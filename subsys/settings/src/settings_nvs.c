/*
 * Copyright (c) 2019 Laczen
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "settings/settings.h"
#include "settings/settings_nvs.h"
#include "settings_priv.h"

#include <logging/log.h>
LOG_MODULE_DECLARE(settings, CONFIG_SETTINGS_LOG_LEVEL);

struct settings_nvs_read_fn_arg {
	struct nvs_fs *fs;
	u16_t id;
};

static int settings_nvs_load(struct settings_store *cs, const char *subtree);
static int settings_nvs_save(struct settings_store *cs, const char *name,
			     const char *value, size_t val_len);

static struct settings_store_itf settings_nvs_itf = {
	.csi_load = settings_nvs_load,
	.csi_save = settings_nvs_save,
};

static ssize_t settings_nvs_read_fn(void *back_end, void *data, size_t len)
{
	struct settings_nvs_read_fn_arg *rd_fn_arg;
	ssize_t rc;

	rd_fn_arg = (struct settings_nvs_read_fn_arg *)back_end;

	rc = nvs_read(rd_fn_arg->fs, rd_fn_arg->id, data, len);
	if (rc > len) {
		/* nvs_read signals that not all bytes were read
		 * align read len to what was requested
		 */
		rc = len;
	}
	return rc;
}

int settings_nvs_src(struct settings_nvs *cf)
{
	cf->cf_store.cs_itf = &settings_nvs_itf;
	settings_src_register(&cf->cf_store);

	return 0;
}

int settings_nvs_dst(struct settings_nvs *cf)
{
	cf->cf_store.cs_itf = &settings_nvs_itf;
	settings_dst_register(&cf->cf_store);

	return 0;
}

static int settings_nvs_load(struct settings_store *cs, const char *subtree)
{
	struct settings_nvs *cf = (struct settings_nvs *)cs;
	struct settings_nvs_read_fn_arg read_fn_arg;
	struct settings_handler *ch;
	char name[SETTINGS_MAX_NAME_LEN + SETTINGS_EXTRA_LEN + 1];
	char buf;
	const char *name_argv;
	ssize_t rc1, rc2;
	u16_t name_id = NVS_NAMECNT_ID;

	name_id = cf->last_name_id + 1;

	while (1) {

		name_id--;
		if (name_id == NVS_NAMECNT_ID) {
			break;
		}

		/* In the NVS backend, each setting item is stored in two NVS
		 * entries one for the setting's name and one with the
		 * setting's value.
		 */
		rc1 = nvs_read(&cf->cf_nvs, name_id, &name, sizeof(name));
		rc2 = nvs_read(&cf->cf_nvs, name_id + NVS_NAME_ID_OFFSET,
			       &buf, sizeof(buf));

		if ((rc1 <= 0) && (rc2 <= 0)) {
			continue;
		}

		if ((rc1 <= 0) || (rc2 <= 0)) {
			/* Settings item is not stored correctly in the NVS.
			 * NVS entry for its name or value is either missing
			 * or deleted. Clean dirty entries to make space for
			 * future settings item.
			 */
			if (name_id == cf->last_name_id) {
				cf->last_name_id--;
				nvs_write(&cf->cf_nvs, NVS_NAMECNT_ID,
					  &cf->last_name_id, sizeof(u16_t));
			}
			nvs_delete(&cf->cf_nvs, name_id);
			nvs_delete(&cf->cf_nvs, name_id + NVS_NAME_ID_OFFSET);
			continue;
		}

		/* Found a name, this might not include a trailing \0 */
		name[rc1] = '\0';

		if (subtree && !settings_name_steq(name, subtree, NULL)) {
			continue;
		}

		ch = settings_parse_and_lookup(name, &name_argv);
		if (!ch) {
			continue;
		}

		read_fn_arg.fs = &cf->cf_nvs;
		read_fn_arg.id = name_id + NVS_NAME_ID_OFFSET;
		ch->h_set(name_argv, rc2, settings_nvs_read_fn,
			  (void *) &read_fn_arg);
	}
	return 0;
}

static int settings_nvs_save(struct settings_store *cs, const char *name,
			     const char *value, size_t val_len)
{
	struct settings_nvs *cf = (struct settings_nvs *)cs;
	char rdname[SETTINGS_MAX_NAME_LEN + SETTINGS_EXTRA_LEN + 1];
	u16_t name_id, write_name_id;
	bool delete, write_name;
	int rc = 0;

	if (!name) {
		return -EINVAL;
	}

	/* Find out if we are doing a delete */
	delete = ((value == NULL) || (val_len == 0));

	name_id = cf->last_name_id + 1;
	write_name_id = cf->last_name_id + 1;
	write_name = true;

	while (1) {
		name_id--;
		if (name_id == NVS_NAMECNT_ID) {
			break;
		}

		rc = nvs_read(&cf->cf_nvs, name_id, &rdname, sizeof(rdname));

		if (rc < 0) {
			/* Error or entry not found */
			if (rc == -ENOENT) {
				write_name_id = name_id;
			}
			continue;
		}

		rdname[rc] = '\0';

		if (strcmp(name, rdname)) {
			continue;
		}

		if ((delete) && (name_id == cf->last_name_id)) {
			cf->last_name_id--;
			rc = nvs_write(&cf->cf_nvs, NVS_NAMECNT_ID,
				       &cf->last_name_id, sizeof(u16_t));
		}

		if (delete) {
			rc = nvs_delete(&cf->cf_nvs, name_id);
			rc = nvs_delete(&cf->cf_nvs, name_id +
					NVS_NAME_ID_OFFSET);

			return 0;
		}
		write_name_id = name_id;
		write_name = false;
		break;
	}

	if (delete) {
		return -ENOENT;
	}

	/* No free IDs left. */
	if (write_name_id == NVS_NAMECNT_ID + NVS_NAME_ID_OFFSET) {
		return -ENOMEM;
	}

	/* write the value */
	rc = nvs_write(&cf->cf_nvs, write_name_id + NVS_NAME_ID_OFFSET,
		       value, val_len);

	/* write the name if required */
	if (write_name) {
		rc = nvs_write(&cf->cf_nvs, write_name_id, name, strlen(name));
	}

	/* update the last_name_id and write to flash if required*/
	if (write_name_id > cf->last_name_id) {
		cf->last_name_id = write_name_id;
		rc = nvs_write(&cf->cf_nvs, NVS_NAMECNT_ID, &cf->last_name_id,
			       sizeof(u16_t));
	}

	if (rc < 0) {
		return rc;
	}

	return 0;
}

/* Initialize the nvs backend. */
int settings_nvs_backend_init(struct settings_nvs *cf)
{
	int rc;
	u16_t last_name_id;

	rc = nvs_init(&cf->cf_nvs, cf->flash_dev_name);
	if (rc) {
		return rc;
	}

	rc = nvs_read(&cf->cf_nvs, NVS_NAMECNT_ID, &last_name_id,
		      sizeof(last_name_id));
	if (rc < 0) {
		cf->last_name_id = NVS_NAMECNT_ID;
	} else {
		cf->last_name_id = last_name_id;
	}

	LOG_DBG("Initialized");
	return 0;
}
