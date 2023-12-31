/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/** @cond PRIVATE */
#define LOG_PREFIX "analog"
/** @endcond */

/**
 * @file
 *
 * Handling and converting analog data.
 */

/**
 * @defgroup grp_analog Analog data handling
 *
 * Handling and converting analog data.
 *
 * @{
 */

struct unit_mq_string {
	uint64_t value;
	const char *str;
};

/* Please use the same order as in enum sr_unit (libsigrok.h). */
static struct unit_mq_string unit_strings[] = {
	{ SR_UNIT_VOLT, "V" },
	{ SR_UNIT_AMPERE, "A" },
	{ SR_UNIT_OHM, "\xe2\x84\xa6" },
	{ SR_UNIT_FARAD, "F" },
	{ SR_UNIT_KELVIN, "K" },
	{ SR_UNIT_CELSIUS, "\xc2\xb0""C" },
	{ SR_UNIT_FAHRENHEIT, "\xc2\xb0""F" },
	{ SR_UNIT_HERTZ, "Hz" },
	{ SR_UNIT_PERCENTAGE, "%" },
	{ SR_UNIT_BOOLEAN, "" },
	{ SR_UNIT_SECOND, "s" },
	{ SR_UNIT_SIEMENS, "S" },
	{ SR_UNIT_DECIBEL_MW, "dBm" },
	{ SR_UNIT_DECIBEL_VOLT, "dBV" },
	{ SR_UNIT_UNITLESS, "" },
	{ SR_UNIT_DECIBEL_SPL, "dB" },
	{ SR_UNIT_CONCENTRATION, "ppm" },
	{ SR_UNIT_REVOLUTIONS_PER_MINUTE, "RPM" },
	{ SR_UNIT_VOLT_AMPERE, "VA" },
	{ SR_UNIT_WATT, "W" },
	{ SR_UNIT_WATT_HOUR, "Wh" },
	{ SR_UNIT_METER_SECOND, "m/s" },
	{ SR_UNIT_HECTOPASCAL, "hPa" },
	{ SR_UNIT_HUMIDITY_293K, "%rF" },
	{ SR_UNIT_DEGREE, "\xc2\xb0" },
	{ SR_UNIT_HENRY, "H" },
	{ SR_UNIT_GRAM, "g" },
	{ SR_UNIT_CARAT, "ct" },
	{ SR_UNIT_OUNCE, "oz" },
	{ SR_UNIT_TROY_OUNCE, "oz t" },
	{ SR_UNIT_POUND, "lb" },
	{ SR_UNIT_PENNYWEIGHT, "dwt" },
	{ SR_UNIT_GRAIN, "gr" },
	{ SR_UNIT_TAEL, "tael" },
	{ SR_UNIT_MOMME, "momme" },
	{ SR_UNIT_TOLA, "tola" },
	{ SR_UNIT_PIECE, "pcs" },
	{ SR_UNIT_JOULE, "J" },
	{ SR_UNIT_COULOMB, "C" },
	{ SR_UNIT_AMPERE_HOUR, "Ah" },
	ALL_ZERO
};

/* Please use the same order as in enum sr_mqflag (libsigrok.h). */
static struct unit_mq_string mq_strings[] = {
	{ SR_MQFLAG_AC, " AC" },
	{ SR_MQFLAG_DC, " DC" },
	{ SR_MQFLAG_RMS, " RMS" },
	{ SR_MQFLAG_DIODE, " DIODE" },
	{ SR_MQFLAG_HOLD, " HOLD" },
	{ SR_MQFLAG_MAX, " MAX" },
	{ SR_MQFLAG_MIN, " MIN" },
	{ SR_MQFLAG_AUTORANGE, " AUTO" },
	{ SR_MQFLAG_RELATIVE, " REL" },
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_A, "(A)" },
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_C, "(C)" },
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_Z, "(Z)" },
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_FLAT, "(SPL)" },
	{ SR_MQFLAG_SPL_TIME_WEIGHT_S, " S" },
	{ SR_MQFLAG_SPL_TIME_WEIGHT_F, " F" },
	{ SR_MQFLAG_SPL_LAT, " LAT" },
	/* Not a standard function for SLMs, so this is a made-up notation. */
	{ SR_MQFLAG_SPL_PCT_OVER_ALARM, "%oA" },
	{ SR_MQFLAG_DURATION, " DURATION" },
	{ SR_MQFLAG_AVG, " AVG" },
	{ SR_MQFLAG_REFERENCE, " REF" },
	{ SR_MQFLAG_UNSTABLE, " UNSTABLE" },
	{ SR_MQFLAG_FOUR_WIRE, " 4-WIRE" },
	ALL_ZERO
};

/** @private */
SR_PRIV int sr_analog_init(struct sr_datafeed_analog *analog,
		struct sr_analog_encoding *encoding,
		struct sr_analog_meaning *meaning,
		struct sr_analog_spec *spec,
		int digits)
{
	memset(analog, 0, sizeof(*analog));
	memset(encoding, 0, sizeof(*encoding));
	memset(meaning, 0, sizeof(*meaning));
	memset(spec, 0, sizeof(*spec));

	analog->encoding = encoding;
	analog->meaning = meaning;
	analog->spec = spec;

	encoding->unitsize = sizeof(float);
	encoding->is_float = TRUE;
#ifdef WORDS_BIGENDIAN
	encoding->is_bigendian = TRUE;
#else
	encoding->is_bigendian = FALSE;
#endif
	encoding->digits = digits;
	encoding->is_digits_decimal = TRUE;
	encoding->scale.p = 1;
	encoding->scale.q = 1;
	encoding->offset.p = 0;
	encoding->offset.q = 1;

	spec->spec_digits = digits;

	return SR_OK;
}

/**
 * Convert an analog datafeed payload to an array of floats.
 *
 * The caller must provide the #outbuf space for the conversion result,
 * and is expected to free allocated space after use.
 *
 * @param[in] analog The analog payload to convert. Must not be NULL.
 *                   analog->data, analog->meaning, and analog->encoding
 *                   must not be NULL.
 * @param[out] outbuf Memory where to store the result. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Unsupported encoding.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.4.0
 */
SR_API int sr_analog_to_float(const struct sr_datafeed_analog *analog,
		float *outbuf)
{
	size_t count;
	gboolean host_bigendian;
	gboolean input_float, input_signed, input_bigendian;
	size_t input_unitsize;
	double scale, offset, value;
	const uint8_t *data8;
	gboolean input_is_native;
	char type_text[10];

	if (!analog || !analog->data || !analog->meaning || !analog->encoding)
		return SR_ERR_ARG;
	if (!outbuf)
		return SR_ERR_ARG;

	count = analog->num_samples * g_slist_length(analog->meaning->channels);

	/*
	 * Determine properties of the input data's and the host's
	 * native formats, to simplify test conditions below.
	 * Error messages for unsupported input property combinations
	 * will only be seen by developers and maintainers of input
	 * formats or acquisition device drivers. Terse output is
	 * acceptable there, users shall never see them.
	 */
#ifdef WORDS_BIGENDIAN
	host_bigendian = TRUE;
#else
	host_bigendian = FALSE;
#endif
	input_float = analog->encoding->is_float;
	input_signed = analog->encoding->is_signed;
	input_bigendian = analog->encoding->is_bigendian;
	input_unitsize = analog->encoding->unitsize;

	/*
	 * Prepare the iteration over the sample data: Get the common
	 * scale/offset factors which apply to all individual values.
	 * Position the read pointer on the first byte of input data.
	 */
	offset = analog->encoding->offset.p;
	offset /= analog->encoding->offset.q;
	scale = analog->encoding->scale.p;
	scale /= analog->encoding->scale.q;
	data8 = analog->data;

	/*
	 * Immediately handle the special case where input data needs
	 * no conversion because it already is in the application's
	 * native format. Do apply scale/offset though when applicable
	 * on our way out.
	 */
	input_is_native = input_float &&
		input_unitsize == sizeof(outbuf[0]) &&
		input_bigendian == host_bigendian;
	if (input_is_native) {
		memcpy(outbuf, data8, count * sizeof(outbuf[0]));
		if (scale != 1.0 || offset != 0.0) {
			while (count--) {
				*outbuf *= scale;
				*outbuf += offset;
				outbuf++;
			}
		}
		return SR_OK;
	}

	/*
	 * Accept sample values in different widths and data types and
	 * endianess formats (floating point or signed or unsigned
	 * integer, in either endianess, for a set of supported widths).
	 * Common scale/offset factors apply to all sample values.
	 *
	 * Do most internal calculations on double precision values.
	 * Only trim the result data to single precision, since that's
	 * the routine's result data type in its public API which needs
	 * to be kept for compatibility. It remains an option for later
	 * to add another public routine which returns double precision
	 * result data, call sites could migrate at their own pace.
	 */
	if (input_float && input_unitsize == sizeof(float)) {
		float (*reader)(const uint8_t **p);
		if (input_bigendian)
			reader = read_fltbe_inc;
		else
			reader = read_fltle_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	if (input_float && input_unitsize == sizeof(double)) {
		double (*reader)(const uint8_t **p);
		if (input_bigendian)
			reader = read_dblbe_inc;
		else
			reader = read_dblle_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	if (input_float) {
		snprintf(type_text, sizeof(type_text), "%c%zu%s",
			'f', input_unitsize * 8, input_bigendian ? "be" : "le");
		sr_err("Unsupported type for analog-to-float conversion: %s.",
			type_text);
		return SR_ERR;
	}

	if (input_unitsize == sizeof(uint8_t) && input_signed) {
		int8_t (*reader)(const uint8_t **p);
		reader = read_i8_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	if (input_unitsize == sizeof(uint8_t)) {
		uint8_t (*reader)(const uint8_t **p);
		reader = read_u8_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	if (input_unitsize == sizeof(uint16_t) && input_signed) {
		int16_t (*reader)(const uint8_t **p);
		if (input_bigendian)
			reader = read_i16be_inc;
		else
			reader = read_i16le_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	if (input_unitsize == sizeof(uint16_t)) {
		uint16_t (*reader)(const uint8_t **p);
		if (input_bigendian)
			reader = read_u16be_inc;
		else
			reader = read_u16le_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	if (input_unitsize == sizeof(uint32_t) && input_signed) {
		int32_t (*reader)(const uint8_t **p);
		if (input_bigendian)
			reader = read_i32be_inc;
		else
			reader = read_i32le_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	if (input_unitsize == sizeof(uint32_t)) {
		uint32_t (*reader)(const uint8_t **p);
		if (input_bigendian)
			reader = read_u32be_inc;
		else
			reader = read_u32le_inc;
		while (count--) {
			value = reader(&data8);
			value *= scale;
			value += offset;
			*outbuf++ = value;
		}
		return SR_OK;
	}
	snprintf(type_text, sizeof(type_text), "%c%zu%s",
		input_float ? 'f' : input_signed ? 'i' : 'u',
		input_unitsize * 8, input_bigendian ? "be" : "le");
	sr_err("Unsupported type for analog-to-float conversion: %s.",
		type_text);
	return SR_ERR;
}

/**
 * Scale a float value to the appropriate SI prefix.
 *
 * @param[in,out] value The float value to convert to appropriate SI prefix.
 * @param[in,out] digits The number of significant decimal digits in value.
 *
 * @return The SI prefix to which value was scaled, as a printable string.
 *
 * @since 0.5.0
 */
SR_API const char *sr_analog_si_prefix(float *value, int *digits)
{
/** @cond PRIVATE */
#define NEG_PREFIX_COUNT 5 /* number of prefixes below unity */
#define POS_PREFIX_COUNT (int)(ARRAY_SIZE(prefixes) - NEG_PREFIX_COUNT - 1)
/** @endcond */
	static const char *prefixes[] = { "f", "p", "n", "µ", "m", "", "k", "M", "G", "T" };

	if (!value || !digits || isnan(*value))
		return prefixes[NEG_PREFIX_COUNT];

	float logval = log10f(fabsf(*value));
	int prefix = (logval / 3) - (logval < 1);

	if (prefix < -NEG_PREFIX_COUNT)
		prefix = -NEG_PREFIX_COUNT;
	if (3 * prefix < -*digits)
		prefix = (-*digits + 2 * (*digits < 0)) / 3;
	if (prefix > POS_PREFIX_COUNT)
		prefix = POS_PREFIX_COUNT;

	*value *= powf(10, -3 * prefix);
	*digits += 3 * prefix;

	return prefixes[prefix + NEG_PREFIX_COUNT];
}

/**
 * Check if a unit "accepts" an SI prefix.
 *
 * E.g. SR_UNIT_VOLT is SI prefix friendly while SR_UNIT_DECIBEL_MW or
 * SR_UNIT_PERCENTAGE are not.
 *
 * @param[in] unit The unit to check for SI prefix "friendliness".
 *
 * @return TRUE if the unit "accept" an SI prefix.
 *
 * @since 0.5.0
 */
SR_API gboolean sr_analog_si_prefix_friendly(enum sr_unit unit)
{
	static const enum sr_unit prefix_friendly_units[] = {
		SR_UNIT_VOLT,
		SR_UNIT_AMPERE,
		SR_UNIT_OHM,
		SR_UNIT_FARAD,
		SR_UNIT_KELVIN,
		SR_UNIT_HERTZ,
		SR_UNIT_SECOND,
		SR_UNIT_SIEMENS,
		SR_UNIT_VOLT_AMPERE,
		SR_UNIT_WATT,
		SR_UNIT_WATT_HOUR,
		SR_UNIT_METER_SECOND,
		SR_UNIT_HENRY,
		SR_UNIT_GRAM
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(prefix_friendly_units); i++)
		if (unit == prefix_friendly_units[i])
			return TRUE;

	return FALSE;
}

/**
 * Convert the unit/MQ/MQ flags in the analog struct to a string.
 *
 * The string is allocated by the function and must be freed by the caller
 * after use by calling g_free().
 *
 * @param[in] analog Struct containing the unit, MQ and MQ flags.
 *                   Must not be NULL. analog->meaning must not be NULL.
 * @param[out] result Pointer to store result. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.4.0
 */
SR_API int sr_analog_unit_to_string(const struct sr_datafeed_analog *analog,
		char **result)
{
	int i;
	GString *buf;

	if (!analog || !(analog->meaning) || !result)
		return SR_ERR_ARG;

	buf = g_string_new(NULL);

	for (i = 0; unit_strings[i].value; i++) {
		if (analog->meaning->unit == unit_strings[i].value) {
			g_string_assign(buf, unit_strings[i].str);
			break;
		}
	}

	/* More than one MQ flag may apply. */
	for (i = 0; mq_strings[i].value; i++)
		if (analog->meaning->mqflags & mq_strings[i].value)
			g_string_append(buf, mq_strings[i].str);

	*result = g_string_free(buf, FALSE);

	return SR_OK;
}

/**
 * Set sr_rational r to the given value.
 *
 * @param[out] r Rational number struct to set. Must not be NULL.
 * @param[in] p Numerator.
 * @param[in] q Denominator.
 *
 * @since 0.4.0
 */
SR_API void sr_rational_set(struct sr_rational *r, int64_t p, uint64_t q)
{
	if (!r)
		return;

	r->p = p;
	r->q = q;
}

#ifndef HAVE___INT128_T
struct sr_int128_t {
	int64_t high;
	uint64_t low;
};

struct sr_uint128_t {
	uint64_t high;
	uint64_t low;
};

static void mult_int64(struct sr_int128_t *res, const int64_t a,
	const int64_t b)
{
	uint64_t t1, t2, t3, t4;

	t1 = (UINT32_MAX & a) * (UINT32_MAX & b);
	t2 = (UINT32_MAX & a) * (b >> 32);
	t3 = (a >> 32) * (UINT32_MAX & b);
	t4 = (a >> 32) * (b >> 32);

	res->low = t1 + (t2 << 32) + (t3 << 32);
	res->high = (t1 >> 32) + (uint64_t)((uint32_t)(t2)) + (uint64_t)((uint32_t)(t3));
	res->high >>= 32;
	res->high += ((int64_t)t2 >> 32) + ((int64_t)t3 >> 32) + t4;
}

static void mult_uint64(struct sr_uint128_t *res, const uint64_t a,
	const uint64_t b)
{
	uint64_t t1, t2, t3, t4;

	// (x1 + x2) * (y1 + y2) = x1*y1 + x1*y2 + x2*y1 + x2*y2
	t1 = (UINT32_MAX & a) * (UINT32_MAX & b);
	t2 = (UINT32_MAX & a) * (b >> 32);
	t3 = (a >> 32) * (UINT32_MAX & b);
	t4 = (a >> 32) * (b >> 32);

	res->low = t1 + (t2 << 32) + (t3 << 32);
	res->high = (t1 >> 32) + (uint64_t)((uint32_t)(t2)) + (uint64_t)((uint32_t)(t3));
	res->high >>= 32;
	res->high += ((int64_t)t2 >> 32) + ((int64_t)t3 >> 32) + t4;
}
#endif

/**
 * Compare two sr_rational for equality.
 *
 * The values are compared for numerical equality, i.e. 2/10 == 1/5.
 *
 * @param[in] a First value.
 * @param[in] b Second value.
 *
 * @retval 1 if both values are equal.
 * @retval 0 Otherwise.
 *
 * @since 0.5.0
 */
SR_API int sr_rational_eq(const struct sr_rational *a, const struct sr_rational *b)
{
#ifdef HAVE___INT128_T
	__int128_t m1, m2;

	/* p1/q1 = p2/q2  <=>  p1*q2 = p2*q1 */
	m1 = ((__int128_t)(b->p)) * ((__uint128_t)a->q);
	m2 = ((__int128_t)(a->p)) * ((__uint128_t)b->q);

	return (m1 == m2);

#else
	struct sr_int128_t m1, m2;

	mult_int64(&m1, a->q, b->p);
	mult_int64(&m2, a->p, b->q);

	return (m1.high == m2.high) && (m1.low == m2.low);
#endif
}

/**
 * Multiply two sr_rational.
 *
 * The resulting nominator/denominator are reduced if the result would not fit
 * otherwise. If the resulting nominator/denominator are relatively prime,
 * this may not be possible.
 *
 * It is safe to use the same variable for result and input values.
 *
 * @param[in] a First value.
 * @param[in] b Second value.
 * @param[out] res Result.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Resulting value too large.
 *
 * @since 0.5.0
 */
SR_API int sr_rational_mult(struct sr_rational *res, const struct sr_rational *a,
	const struct sr_rational *b)
{
#ifdef HAVE___INT128_T
	__int128_t p;
	__uint128_t q;

	p = (__int128_t)(a->p) * (__int128_t)(b->p);
	q = (__uint128_t)(a->q) * (__uint128_t)(b->q);

	if ((p > INT64_MAX) || (p < INT64_MIN) || (q > UINT64_MAX)) {
		while (!((p & 1) || (q & 1))) {
			p /= 2;
			q /= 2;
		}
	}

	if ((p > INT64_MAX) || (p < INT64_MIN) || (q > UINT64_MAX)) {
		// TODO: determine gcd to do further reduction
		return SR_ERR_ARG;
	}

	res->p = (int64_t)p;
	res->q = (uint64_t)q;

	return SR_OK;

#else
	struct sr_int128_t p;
	struct sr_uint128_t q;

	mult_int64(&p, a->p, b->p);
	mult_uint64(&q, a->q, b->q);

	while (!(p.low & 1) && !(q.low & 1)) {
		p.low /= 2;
		if (p.high & 1)
			p.low |= (1ll << 63);
		p.high >>= 1;
		q.low /= 2;
		if (q.high & 1)
			q.low |= (1ll << 63);
		q.high >>= 1;
	}

	if (q.high)
		return SR_ERR_ARG;
	if ((p.high >= 0) && (p.low > INT64_MAX))
		return SR_ERR_ARG;
	if (p.high < -1)
		return SR_ERR_ARG;

	res->p = (int64_t)p.low;
	res->q = q.low;

	return SR_OK;
#endif
}

/**
 * Divide rational a by rational b.
 *
 * The resulting nominator/denominator are reduced if the result would not fit
 * otherwise. If the resulting nominator/denominator are relatively prime,
 * this may not be possible.
 *
 * It is safe to use the same variable for result and input values.
 *
 * @param[in] num Numerator.
 * @param[in] div Divisor.
 * @param[out] res Result.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Division by zero, denominator of divisor too large,
 *                    or resulting value too large.
 *
 * @since 0.5.0
 */
SR_API int sr_rational_div(struct sr_rational *res, const struct sr_rational *num,
	const struct sr_rational *div)
{
	struct sr_rational t;

	if (div->q > INT64_MAX)
		return SR_ERR_ARG;
	if (div->p == 0)
		return SR_ERR_ARG;

	if (div->p > 0) {
		t.p = div->q;
		t.q = div->p;
	} else {
		t.p = -div->q;
		t.q = -div->p;
	}

	return sr_rational_mult(res, num, &t);
}

/** @} */
