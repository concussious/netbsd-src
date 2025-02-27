/*	$NetBSD: region.c,v 1.6 2025/01/26 16:25:38 christos Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

/*! \file */

#include <stdlib.h>
#include <string.h>

#include <isc/region.h>
#include <isc/util.h>

int
isc_region_compare(isc_region_t *r1, isc_region_t *r2) {
	unsigned int l;
	int result;

	REQUIRE(r1 != NULL);
	REQUIRE(r2 != NULL);
	REQUIRE(r1->base != NULL);
	REQUIRE(r2->base != NULL);

	l = (r1->length < r2->length) ? r1->length : r2->length;

	if ((result = memcmp(r1->base, r2->base, l)) != 0) {
		return (result < 0) ? -1 : 1;
	} else {
		return (r1->length == r2->length)  ? 0
		       : (r1->length < r2->length) ? -1
						   : 1;
	}
}
