/*
 *  X.509 base functions for creating certificates / CSRs
 *
 *  Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 *  Copyright (C) 2015-2018 Tempesta Technologies, Inc.
 *  SPDX-License-Identifier: GPL-2.0
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */

#if !defined(TTLS_CONFIG_FILE)
#include "config.h"
#else
#include TTLS_CONFIG_FILE
#endif

#if defined(TTLS_X509_CREATE_C)

#include "x509.h"
#include "asn1write.h"
#include "oid.h"

#include <string.h>

typedef struct {
	const char *name;
	size_t name_len;
	const char*oid;
} x509_attr_descriptor_t;

#define ADD_STRLEN(s)	 s, sizeof(s) - 1

static const x509_attr_descriptor_t x509_attrs[] =
{
	{ ADD_STRLEN("CN"),					   TTLS_OID_AT_CN },
	{ ADD_STRLEN("commonName"),			   TTLS_OID_AT_CN },
	{ ADD_STRLEN("C"),						TTLS_OID_AT_COUNTRY },
	{ ADD_STRLEN("countryName"),			  TTLS_OID_AT_COUNTRY },
	{ ADD_STRLEN("O"),						TTLS_OID_AT_ORGANIZATION },
	{ ADD_STRLEN("organizationName"),		 TTLS_OID_AT_ORGANIZATION },
	{ ADD_STRLEN("L"),						TTLS_OID_AT_LOCALITY },
	{ ADD_STRLEN("locality"),				 TTLS_OID_AT_LOCALITY },
	{ ADD_STRLEN("R"),						TTLS_OID_PKCS9_EMAIL },
	{ ADD_STRLEN("OU"),					   TTLS_OID_AT_ORG_UNIT },
	{ ADD_STRLEN("organizationalUnitName"),   TTLS_OID_AT_ORG_UNIT },
	{ ADD_STRLEN("ST"),					   TTLS_OID_AT_STATE },
	{ ADD_STRLEN("stateOrProvinceName"),	  TTLS_OID_AT_STATE },
	{ ADD_STRLEN("emailAddress"),			 TTLS_OID_PKCS9_EMAIL },
	{ ADD_STRLEN("serialNumber"),			 TTLS_OID_AT_SERIAL_NUMBER },
	{ ADD_STRLEN("postalAddress"),			TTLS_OID_AT_POSTAL_ADDRESS },
	{ ADD_STRLEN("postalCode"),			   TTLS_OID_AT_POSTAL_CODE },
	{ ADD_STRLEN("dnQualifier"),			  TTLS_OID_AT_DN_QUALIFIER },
	{ ADD_STRLEN("title"),					TTLS_OID_AT_TITLE },
	{ ADD_STRLEN("surName"),				  TTLS_OID_AT_SUR_NAME },
	{ ADD_STRLEN("SN"),					   TTLS_OID_AT_SUR_NAME },
	{ ADD_STRLEN("givenName"),				TTLS_OID_AT_GIVEN_NAME },
	{ ADD_STRLEN("GN"),					   TTLS_OID_AT_GIVEN_NAME },
	{ ADD_STRLEN("initials"),				 TTLS_OID_AT_INITIALS },
	{ ADD_STRLEN("pseudonym"),				TTLS_OID_AT_PSEUDONYM },
	{ ADD_STRLEN("generationQualifier"),	  TTLS_OID_AT_GENERATION_QUALIFIER },
	{ ADD_STRLEN("domainComponent"),		  TTLS_OID_DOMAIN_COMPONENT },
	{ ADD_STRLEN("DC"),					   TTLS_OID_DOMAIN_COMPONENT },
	{ NULL, 0, NULL }
};

static const char *x509_at_oid_from_name(const char *name, size_t name_len)
{
	const x509_attr_descriptor_t *cur;

	for (cur = x509_attrs; cur->name != NULL; cur++)
		if (cur->name_len == name_len &&
			strncmp(cur->name, name, name_len) == 0)
			break;

	return(cur->oid);
}

int ttls_x509_string_to_names(ttls_asn1_named_data **head, const char *name)
{
	int ret = 0;
	const char *s = name, *c = s;
	const char *end = s + strlen(s);
	const char *oid = NULL;
	int in_tag = 1;
	char data[TTLS_X509_MAX_DN_NAME_SIZE];
	char *d = data;

	/* Clear existing chain if present */
	ttls_asn1_free_named_data_list(head);

	while (c <= end)
	{
		if (in_tag && *c == '=')
		{
			if ((oid = x509_at_oid_from_name(s, c - s)) == NULL)
			{
				ret = TTLS_ERR_X509_UNKNOWN_OID;
				goto exit;
			}

			s = c + 1;
			in_tag = 0;
			d = data;
		}

		if (!in_tag && *c == '\\' && c != end)
		{
			c++;

			/* Check for valid escaped characters */
			if (c == end || *c != ',')
			{
				ret = TTLS_ERR_X509_INVALID_NAME;
				goto exit;
			}
		}
		else if (!in_tag && (*c == ',' || c == end))
		{
			if (ttls_asn1_store_named_data(head, oid, strlen(oid),
									   (unsigned char *) data,
									   d - data) == NULL)
			{
				return(TTLS_ERR_X509_ALLOC_FAILED);
			}

			while (c < end && *(c + 1) == ' ')
				c++;

			s = c + 1;
			in_tag = 1;
		}

		if (!in_tag && s != c + 1)
		{
			*(d++) = *c;

			if (d - data == TTLS_X509_MAX_DN_NAME_SIZE)
			{
				ret = TTLS_ERR_X509_INVALID_NAME;
				goto exit;
			}
		}

		c++;
	}

exit:

	return ret;
}

/* The first byte of the value in the ttls_asn1_named_data structure is reserved
 * to store the critical boolean for us
 */
int ttls_x509_set_extension(ttls_asn1_named_data **head, const char *oid, size_t oid_len,
						int critical, const unsigned char *val, size_t val_len)
{
	ttls_asn1_named_data *cur;

	if ((cur = ttls_asn1_store_named_data(head, oid, oid_len,
									   NULL, val_len + 1)) == NULL)
	{
		return(TTLS_ERR_X509_ALLOC_FAILED);
	}

	cur->val.p[0] = critical;
	memcpy(cur->val.p + 1, val, val_len);

	return 0;
}

/*
 *  RelativeDistinguishedName ::=
 *	SET OF AttributeTypeAndValue
 *
 *  AttributeTypeAndValue ::= SEQUENCE {
 *	type	 AttributeType,
 *	value	AttributeValue }
 *
 *  AttributeType ::= OBJECT IDENTIFIER
 *
 *  AttributeValue ::= ANY DEFINED BY AttributeType
 */
static int x509_write_name(unsigned char **p, unsigned char *start,
							const char *oid, size_t oid_len,
							const unsigned char *name, size_t name_len)
{
	int ret;
	size_t len = 0;

	// Write PrintableString for all except TTLS_OID_PKCS9_EMAIL
	//
	if (TTLS_OID_SIZE(TTLS_OID_PKCS9_EMAIL) == oid_len &&
		memcmp(oid, TTLS_OID_PKCS9_EMAIL, oid_len) == 0)
	{
		TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_ia5_string(p, start,
												  (const char *) name,
												  name_len));
	}
	else
	{
		TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_printable_string(p, start,
														(const char *) name,
														name_len));
	}

	// Write OID
	//
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_oid(p, start, oid, oid_len));

	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_len(p, start, len));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_tag(p, start, TTLS_ASN1_CONSTRUCTED |
												 TTLS_ASN1_SEQUENCE));

	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_len(p, start, len));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_tag(p, start, TTLS_ASN1_CONSTRUCTED |
												 TTLS_ASN1_SET));

	return((int) len);
}

int ttls_x509_write_names(unsigned char **p, unsigned char *start,
					  ttls_asn1_named_data *first)
{
	int ret;
	size_t len = 0;
	ttls_asn1_named_data *cur = first;

	while (cur != NULL)
	{
		TTLS_ASN1_CHK_ADD(len, x509_write_name(p, start, (char *) cur->oid.p,
											cur->oid.len,
											cur->val.p, cur->val.len));
		cur = cur->next;
	}

	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_len(p, start, len));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_tag(p, start, TTLS_ASN1_CONSTRUCTED |
												 TTLS_ASN1_SEQUENCE));

	return((int) len);
}

int ttls_x509_write_sig(unsigned char **p, unsigned char *start,
					const char *oid, size_t oid_len,
					unsigned char *sig, size_t size)
{
	int ret;
	size_t len = 0;

	if (*p < start || (size_t)(*p - start) < size)
		return(TTLS_ERR_ASN1_BUF_TOO_SMALL);

	len = size;
	(*p) -= len;
	memcpy(*p, sig, len);

	if (*p - start < 1)
		return(TTLS_ERR_ASN1_BUF_TOO_SMALL);

	*--(*p) = 0;
	len += 1;

	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_len(p, start, len));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_tag(p, start, TTLS_ASN1_BIT_STRING));

	// Write OID
	//
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_algorithm_identifier(p, start, oid,
														oid_len, 0));

	return((int) len);
}

static int x509_write_extension(unsigned char **p, unsigned char *start,
								 ttls_asn1_named_data *ext)
{
	int ret;
	size_t len = 0;

	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_raw_buffer(p, start, ext->val.p + 1,
											  ext->val.len - 1));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_len(p, start, ext->val.len - 1));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_tag(p, start, TTLS_ASN1_OCTET_STRING));

	if (ext->val.p[0] != 0)
	{
		TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_bool(p, start, 1));
	}

	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_raw_buffer(p, start, ext->oid.p,
											  ext->oid.len));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_len(p, start, ext->oid.len));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_tag(p, start, TTLS_ASN1_OID));

	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_len(p, start, len));
	TTLS_ASN1_CHK_ADD(len, ttls_asn1_write_tag(p, start, TTLS_ASN1_CONSTRUCTED |
												 TTLS_ASN1_SEQUENCE));

	return((int) len);
}

/*
 * Extension  ::=  SEQUENCE  {
 *	 extnID	  OBJECT IDENTIFIER,
 *	 critical	BOOLEAN DEFAULT FALSE,
 *	 extnValue   OCTET STRING
 *				 -- contains the DER encoding of an ASN.1 value
 *				 -- corresponding to the extension type identified
 *				 -- by extnID
 *	 }
 */
int ttls_x509_write_extensions(unsigned char **p, unsigned char *start,
						   ttls_asn1_named_data *first)
{
	int ret;
	size_t len = 0;
	ttls_asn1_named_data *cur_ext = first;

	while (cur_ext != NULL)
	{
		TTLS_ASN1_CHK_ADD(len, x509_write_extension(p, start, cur_ext));
		cur_ext = cur_ext->next;
	}

	return((int) len);
}

#endif /* TTLS_X509_CREATE_C */
