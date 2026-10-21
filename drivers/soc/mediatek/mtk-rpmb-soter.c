// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek eMMC RPMB access for the Microtrust (soter) TEE daemon
 *
 * teei_daemon forwards the TEE's RPMB requests through /dev/rpmb0 using
 * the ioctl interface of the vendor rpmb-mtk driver. The TEE builds and
 * authenticates the frames itself; the kernel only moves them to and
 * from the eMMC RPMB partition. Implement that interface on top of the
 * RPMB subsystem, which the mmc block driver registers with.
 *
 * Ioctl argument for the frame commands: __u32 size, then size bytes of
 * 512-byte RPMB frames. Results are copied back in place: the response
 * frame replaces the first frame for writes and key programming, and
 * all frames are filled for reads.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mmc/card.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/rpmb.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define MTK_RPMB_NAME			"rpmb"
#define MTK_RPMB_FRAME_SIZE		512
#define MTK_RPMB_BUF_SIZE		(24 * 1024)

#define MTK_RPMB_SOTER_WRITE_DATA	5
#define MTK_RPMB_SOTER_READ_DATA	6
#define MTK_RPMB_SOTER_GET_CNT		7
#define MTK_RPMB_SOTER_GET_WR_SIZE	8
#define MTK_RPMB_SOTER_SET_KEY		9

#define MTK_RPMB_REQ_GET_WRITE_COUNTER	0x2

/* JEDEC RPMB frame */
struct mtk_rpmb_frame {
	u8 stuff[196];
	u8 mac[32];
	u8 data[256];
	u8 nonce[16];
	__be32 write_counter;
	__be16 addr;
	__be16 block_count;
	__be16 result;
	__be16 req_resp;
} __packed;

static_assert(sizeof(struct mtk_rpmb_frame) == MTK_RPMB_FRAME_SIZE);

static DEFINE_MUTEX(mtk_rpmb_lock);
static u8 *mtk_rpmb_buf;
static dev_t mtk_rpmb_devt;
static struct cdev mtk_rpmb_cdev;
static struct class *mtk_rpmb_class;

static int mtk_rpmb_match_emmc(struct device *dev, const void *data)
{
	return to_rpmb_dev(dev)->descr.type == RPMB_TYPE_EMMC;
}

static struct rpmb_dev *mtk_rpmb_get_dev(void)
{
	return rpmb_dev_find_device(NULL, NULL, mtk_rpmb_match_emmc);
}

static int mtk_rpmb_route(struct rpmb_dev *rdev, u8 *frames, u32 nframes,
			  unsigned int cmd)
{
	unsigned int len = nframes * MTK_RPMB_FRAME_SIZE;
	u8 *resp;
	int ret;

	switch (cmd) {
	case MTK_RPMB_SOTER_READ_DATA:
		/* One request frame, nframes response frames. */
		resp = kzalloc(len, GFP_KERNEL);
		if (!resp)
			return -ENOMEM;
		ret = rpmb_route_frames(rdev, frames, MTK_RPMB_FRAME_SIZE,
					resp, len);
		if (!ret)
			memcpy(frames, resp, len);
		break;
	case MTK_RPMB_SOTER_WRITE_DATA:
	case MTK_RPMB_SOTER_SET_KEY:
		/* nframes request frames, one response frame. */
		resp = kzalloc(MTK_RPMB_FRAME_SIZE, GFP_KERNEL);
		if (!resp)
			return -ENOMEM;
		if (cmd == MTK_RPMB_SOTER_SET_KEY)
			len = MTK_RPMB_FRAME_SIZE;
		ret = rpmb_route_frames(rdev, frames, len,
					resp, MTK_RPMB_FRAME_SIZE);
		if (!ret) {
			memset(frames, 0, len);
			memcpy(frames, resp, MTK_RPMB_FRAME_SIZE);
		}
		break;
	default:
		return -ENOTTY;
	}

	kfree(resp);
	return ret;
}

static int mtk_rpmb_get_wc(struct rpmb_dev *rdev, u32 *wc)
{
	struct mtk_rpmb_frame *req, *resp;
	int ret;

	req = kzalloc(2 * sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	resp = req + 1;

	get_random_bytes(req->nonce, sizeof(req->nonce));
	req->req_resp = cpu_to_be16(MTK_RPMB_REQ_GET_WRITE_COUNTER);

	ret = rpmb_route_frames(rdev, (u8 *)req, sizeof(*req),
				(u8 *)resp, sizeof(*resp));
	if (ret)
		goto out;

	if (memcmp(req->nonce, resp->nonce, sizeof(req->nonce))) {
		ret = -EIO;
		goto out;
	}
	if (resp->result) {
		ret = be16_to_cpu(resp->result);
		goto out;
	}
	*wc = be32_to_cpu(resp->write_counter);
out:
	kfree(req);
	return ret;
}

static long mtk_rpmb_frames_ioctl(struct rpmb_dev *rdev, unsigned int cmd,
				  void __user *arg)
{
	u32 size;
	int ret;

	if (copy_from_user(&size, arg, sizeof(size)))
		return -EFAULT;
	if (!size || size % MTK_RPMB_FRAME_SIZE ||
	    size > MTK_RPMB_BUF_SIZE - sizeof(size))
		return -EINVAL;

	mutex_lock(&mtk_rpmb_lock);
	if (copy_from_user(mtk_rpmb_buf, arg, sizeof(size) + size)) {
		ret = -EFAULT;
		goto unlock;
	}

	ret = mtk_rpmb_route(rdev, mtk_rpmb_buf + sizeof(size),
			     size / MTK_RPMB_FRAME_SIZE, cmd);
	if (ret) {
		pr_err("mtk-rpmb: cmd %u (%u bytes) failed: %d\n",
		       cmd, size, ret);
		goto unlock;
	}

	if (copy_to_user(arg, mtk_rpmb_buf, sizeof(size) + size))
		ret = -EFAULT;
unlock:
	mutex_unlock(&mtk_rpmb_lock);
	return ret;
}

static long mtk_rpmb_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	struct rpmb_dev *rdev;
	struct mmc_card *card;
	u32 val = 0;
	long ret;

	rdev = mtk_rpmb_get_dev();
	if (!rdev)
		return -ENODEV;

	switch (cmd) {
	case MTK_RPMB_SOTER_WRITE_DATA:
	case MTK_RPMB_SOTER_READ_DATA:
	case MTK_RPMB_SOTER_SET_KEY:
		ret = mtk_rpmb_frames_ioctl(rdev, cmd, uarg);
		break;
	case MTK_RPMB_SOTER_GET_CNT:
		mutex_lock(&mtk_rpmb_lock);
		ret = mtk_rpmb_get_wc(rdev, &val);
		mutex_unlock(&mtk_rpmb_lock);
		if (!ret && copy_to_user(uarg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	case MTK_RPMB_SOTER_GET_WR_SIZE:
		/* rdev -> mmc_rpmb_data -> mmc_card */
		card = container_of(rdev->dev.parent->parent,
				    struct mmc_card, dev);
		val = card->ext_csd.rel_sectors;
		ret = copy_to_user(uarg, &val, sizeof(val)) ? -EFAULT : 0;
		break;
	default:
		pr_err("mtk-rpmb: unsupported ioctl %u\n", cmd);
		ret = -ENOTTY;
	}

	rpmb_dev_put(rdev);
	return ret;
}

static const struct file_operations mtk_rpmb_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mtk_rpmb_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static int __init mtk_rpmb_init(void)
{
	struct device *dev;
	int ret;

	mtk_rpmb_buf = kzalloc(MTK_RPMB_BUF_SIZE, GFP_KERNEL);
	if (!mtk_rpmb_buf)
		return -ENOMEM;

	ret = alloc_chrdev_region(&mtk_rpmb_devt, 0, 1, MTK_RPMB_NAME);
	if (ret)
		goto free_buf;

	cdev_init(&mtk_rpmb_cdev, &mtk_rpmb_fops);
	ret = cdev_add(&mtk_rpmb_cdev, mtk_rpmb_devt, 1);
	if (ret)
		goto unregister_region;

	/* "rpmb" is taken by the RPMB subsystem class */
	mtk_rpmb_class = class_create("mtk_rpmb");
	if (IS_ERR(mtk_rpmb_class)) {
		ret = PTR_ERR(mtk_rpmb_class);
		goto del_cdev;
	}

	dev = device_create(mtk_rpmb_class, NULL, mtk_rpmb_devt, NULL,
			    MTK_RPMB_NAME "%d", 0);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto destroy_class;
	}

	return 0;

destroy_class:
	class_destroy(mtk_rpmb_class);
del_cdev:
	cdev_del(&mtk_rpmb_cdev);
unregister_region:
	unregister_chrdev_region(mtk_rpmb_devt, 1);
free_buf:
	kfree(mtk_rpmb_buf);
	return ret;
}
module_init(mtk_rpmb_init);

MODULE_DESCRIPTION("MediaTek eMMC RPMB interface for the Microtrust TEE");
MODULE_LICENSE("GPL");
