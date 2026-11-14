// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi Alioth Read-Only Partition RAM Shadow / Overlay Driver
 *
 * Intercepts bio requests to critical partitions (such as fsg, fsc, modem)
 * and keeps modifications in RAM without writing them to physical flash.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#define ALIOTH_MAX_SHADOWS 8
#define ALIOTH_PARTS_MAX   256

static char alioth_ro_parts[ALIOTH_PARTS_MAX];
static char alioth_rw_parts[ALIOTH_PARTS_MAX];

struct alioth_shadow {
	struct block_device *bdev;
	sector_t start_sector;
	sector_t nr_sectors;
	void *ram_buf;
	struct mutex lock;
	struct completion fill_done;
	struct work_struct fill_work;
	int fill_status;
	bool write_logged;
	char name[64];
};

static struct alioth_shadow alioth_shadows[ALIOTH_MAX_SHADOWS];
static int alioth_nr_shadows;

bool alioth_shadow_bio(struct bio *bio);
int alioth_shadow_add(struct block_device *bdev, sector_t start, sector_t nr, const char *name);

static int __init alioth_ro_parts_setup(char *str)
{
	strscpy(alioth_ro_parts, str, sizeof(alioth_ro_parts));
	return 1;
}
__setup("alioth.ro_parts=", alioth_ro_parts_setup);

static int __init alioth_rw_parts_setup(char *str)
{
	strscpy(alioth_rw_parts, str, sizeof(alioth_rw_parts));
	return 1;
}
__setup("alioth.rw_parts=", alioth_rw_parts_setup);

static bool alioth_name_in_list(const char *list, const char *name)
{
	size_t len = strlen(name);
	const char *p = list;

	while (*p) {
		const char *comma = strchrnul(p, ',');
		size_t token_len = comma - p;

		if ((token_len == 1 && *p == '*') ||
		    (token_len == len && !strncmp(p, name, len)))
			return true;

		p = *comma ? comma + 1 : comma;
	}
	return false;
}

static struct alioth_shadow *alioth_shadow_find(struct block_device *bdev)
{
	int i;

	for (i = 0; i < alioth_nr_shadows; i++) {
		if (alioth_shadows[i].bdev == bdev)
			return &alioth_shadows[i];
	}
	return NULL;
}

static void alioth_shadow_fill(struct work_struct *work)
{
	struct alioth_shadow *s = container_of(work, struct alioth_shadow, fill_work);
	size_t total_bytes = s->nr_sectors * 512;
	size_t done = 0;
	int ret = 0;

	while (done < total_bytes) {
		size_t chunk = min_t(size_t, total_bytes - done, PAGE_SIZE);
		struct page *page = vmalloc_to_page(s->ram_buf + done);
		struct bio *bio = bio_alloc(s->bdev, 1, REQ_OP_READ, GFP_KERNEL);

		if (!bio) {
			ret = -ENOMEM;
			break;
		}

		bio->bi_iter.bi_sector = s->start_sector + (done >> 9);
		if (!bio_add_page(bio, page, chunk, offset_in_page(s->ram_buf + done))) {
			bio_put(bio);
			ret = -EIO;
			break;
		}

		ret = submit_bio_wait(bio);
		bio_put(bio);

		if (ret)
			break;

		done += chunk;
	}

	s->fill_status = ret;
	if (ret)
		pr_err("alioth-shadow: %s: reading partition failed: %d\n", s->name, ret);
	else
		pr_info("alioth-shadow: %s: %zu KiB in RAM, writes stay in RAM\n",
			s->name, total_bytes / 1024);

	complete_all(&s->fill_done);
}

int alioth_shadow_add(struct block_device *bdev, sector_t start, sector_t nr, const char *name)
{
	struct alioth_shadow *s;

	if (alioth_nr_shadows >= ALIOTH_MAX_SHADOWS)
		return -ENOSPC;

	if (!alioth_name_in_list(alioth_ro_parts, name) &&
	    !alioth_name_in_list(alioth_rw_parts, name))
		return 0;

	s = &alioth_shadows[alioth_nr_shadows];
	s->bdev = bdev;
	s->start_sector = start;
	s->nr_sectors = nr;
	strscpy(s->name, name, sizeof(s->name));

	s->ram_buf = vmalloc(nr * 512);
	if (!s->ram_buf)
		return -ENOMEM;

	mutex_init(&s->lock);
	init_completion(&s->fill_done);
	INIT_WORK(&s->fill_work, alioth_shadow_fill);
	s->write_logged = false;

	alioth_nr_shadows++;
	schedule_work(&s->fill_work);

	return 0;
}
EXPORT_SYMBOL_GPL(alioth_shadow_add);

static int alioth_shadow_rw(struct alioth_shadow *s, struct bio *bio)
{
	struct bvec_iter iter;
	struct bio_vec bvec;
	sector_t sector = bio->bi_iter.bi_sector;

	bio_for_each_segment(bvec, bio, iter) {
		void *kaddr = kmap_local_page(bvec.bv_page);
		void *src = kaddr + bvec.bv_offset;
		void *dst = s->ram_buf + (sector * 512);

		if (bio_data_dir(bio) == WRITE)
			memcpy(dst, src, bvec.bv_len);
		else
			memcpy(src, dst, bvec.bv_len);

		kunmap_local(kaddr);
		sector += bvec.bv_len >> 9;
	}

	if (bio_data_dir(bio) == WRITE && !s->write_logged) {
		s->write_logged = true;
		pr_info("alioth-shadow: %s: first write, kept in RAM only\n", s->name);
	}

	return 0;
}

bool alioth_shadow_bio(struct bio *bio)
{
	struct alioth_shadow *s;

	if (!alioth_nr_shadows || !bio->bi_bdev)
		return false;

	s = alioth_shadow_find(bio->bi_bdev);
	if (!s)
		return false;

	wait_for_completion(&s->fill_done);
	if (s->fill_status) {
		bio->bi_status = BLK_STS_IOERR;
		bio_endio(bio);
		return true;
	}

	mutex_lock(&s->lock);
	alioth_shadow_rw(s, bio);
	mutex_unlock(&s->lock);

	bio->bi_status = BLK_STS_OK;
	bio_endio(bio);
	return true;
}
EXPORT_SYMBOL_GPL(alioth_shadow_bio);

MODULE_DESCRIPTION("Xiaomi Alioth Read-Only Partition RAM Shadow / Overlay Driver");
MODULE_LICENSE("GPL");
