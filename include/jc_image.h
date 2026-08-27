/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_image.h - image helpers for vision input (M29).
 *
 * M29a provides only the pure media-type detector; jc_image_load (read + sniff +
 * base64, through the path fence) lands in M29b with the data model.
 */
#ifndef JC_IMAGE_H
#define JC_IMAGE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Map `path`'s extension to an image media type (a static string literal), or
 * NULL when the extension is not a supported image type. Case-insensitive.
 * Supported: .png, .jpg/.jpeg, .gif, .webp. Pure. */
const char *jc_image_media_type(const char *path);

/* Map `path`'s extension to an image-generation output format token ("png",
 * "jpeg", "webp"), or NULL when the extension is not a supported output type.
 * Case-insensitive. Used by generate_image (M32) to pick the API output format
 * and to validate the destination path. Pure. */
const char *jc_image_gen_format(const char *path);

#ifdef __cplusplus
}
#endif
#endif /* JC_IMAGE_H */
