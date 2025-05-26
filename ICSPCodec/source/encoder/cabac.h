/*****************************************************************************
 * cabac.h: arithmetic coder
 *****************************************************************************
 * Copyright (C) 2003-2025 x264 project
 *
 * Authors: Loren Merritt <lorenm@u.washington.edu>
 *          Laurent Aimar <fenrir@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@x264.com.
 *****************************************************************************/

#ifndef X264_CABAC_H
#define X264_CABAC_H
#ifdef __cplusplus
extern "C" {
#endif
void x264_cabac_init();
#ifndef BIT_DEPTH
#define BIT_DEPTH 255
#endif
#define QP_BD_OFFSET (6*(BIT_DEPTH-8))
// #define QP_MAX_SPEC (51+QP_BD_OFFSET)
#define QP_MAX (QP_MAX_SPEC+18)
#define PIXEL_MAX ((1 << BIT_DEPTH)-1)
#define X264_MIN(a,b) ( (a)<(b) ? (a) : (b) )
#define X264_MAX(a,b) ( (a)>(b) ? (a) : (b) )
#define CTX_IDX_DC  4
#define CTX_IDX_AC_START  5
#define X264_MIN3(a,b,c) X264_MIN((a),X264_MIN((b),(c)))
#define X264_MAX3(a,b,c) X264_MAX((a),X264_MAX((b),(c)))
#define X264_MIN4(a,b,c,d) X264_MIN((a),X264_MIN3((b),(c),(d)))
#define X264_MAX4(a,b,c,d) X264_MAX((a),X264_MAX3((b),(c),(d)))
#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#define UNUSED __attribute__((unused))
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NOINLINE __attribute__((noinline))
#define MAY_ALIAS __attribute__((may_alias))
#define x264_constant_p(x) __builtin_constant_p(x)
#define x264_nonconstant_p(x) (!__builtin_constant_p(x))
#else
#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#define NOINLINE __declspec(noinline)
#else
#define ALWAYS_INLINE inline
#define NOINLINE
#endif
#define UNUSED
#define MAY_ALIAS
#define x264_constant_p(x) 0
#define x264_nonconstant_p(x) 0
#endif
#define XCHG(type,a,b) do { type t = a; a = b; b = t; } while( 0 )
#define FIX8(f) ((int)(f*(1<<8)+.5))
#define ARRAY_ELEMS(a) ((int)((sizeof(a))/(sizeof(a[0]))))
#define ALIGN(x,a) (((x)+((a)-1))&~((a)-1))
#define IS_DISPOSABLE(type) ( type == X264_TYPE_B )
#include <stdint.h>
// — your CABAC headers/globals —
#define QP_MAX_SPEC 51
#define CHROMA444   0
#define CABAC_CTX_COUNT 460
#ifdef _MSC_VER
#define DECLARE_ALIGNED( var, n ) __declspec(align(n)) var
#else
#define DECLARE_ALIGNED( var, n ) var __attribute__((aligned(n)))
#endif

#define ALIGNED_4( var )  DECLARE_ALIGNED( var, 4 )
#define ALIGNED_8( var )  DECLARE_ALIGNED( var, 8 )
#define ALIGNED_16( var ) DECLARE_ALIGNED( var, 16 )
#define ALIGNED_64( var ) DECLARE_ALIGNED( var, 64 )

/* -ln2(probability) */
const uint16_t x264_cabac_entropy[128] =
{
    FIX8(0.0273), FIX8(5.7370), FIX8(0.0288), FIX8(5.6618),
    FIX8(0.0303), FIX8(5.5866), FIX8(0.0320), FIX8(5.5114),
    FIX8(0.0337), FIX8(5.4362), FIX8(0.0355), FIX8(5.3610),
    FIX8(0.0375), FIX8(5.2859), FIX8(0.0395), FIX8(5.2106),
    FIX8(0.0416), FIX8(5.1354), FIX8(0.0439), FIX8(5.0602),
    FIX8(0.0463), FIX8(4.9851), FIX8(0.0488), FIX8(4.9099),
    FIX8(0.0515), FIX8(4.8347), FIX8(0.0543), FIX8(4.7595),
    FIX8(0.0572), FIX8(4.6843), FIX8(0.0604), FIX8(4.6091),
    FIX8(0.0637), FIX8(4.5339), FIX8(0.0671), FIX8(4.4588),
    FIX8(0.0708), FIX8(4.3836), FIX8(0.0747), FIX8(4.3083),
    FIX8(0.0788), FIX8(4.2332), FIX8(0.0832), FIX8(4.1580),
    FIX8(0.0878), FIX8(4.0828), FIX8(0.0926), FIX8(4.0076),
    FIX8(0.0977), FIX8(3.9324), FIX8(0.1032), FIX8(3.8572),
    FIX8(0.1089), FIX8(3.7820), FIX8(0.1149), FIX8(3.7068),
    FIX8(0.1214), FIX8(3.6316), FIX8(0.1282), FIX8(3.5565),
    FIX8(0.1353), FIX8(3.4813), FIX8(0.1429), FIX8(3.4061),
    FIX8(0.1510), FIX8(3.3309), FIX8(0.1596), FIX8(3.2557),
    FIX8(0.1686), FIX8(3.1805), FIX8(0.1782), FIX8(3.1053),
    FIX8(0.1884), FIX8(3.0301), FIX8(0.1992), FIX8(2.9549),
    FIX8(0.2107), FIX8(2.8797), FIX8(0.2229), FIX8(2.8046),
    FIX8(0.2358), FIX8(2.7294), FIX8(0.2496), FIX8(2.6542),
    FIX8(0.2642), FIX8(2.5790), FIX8(0.2798), FIX8(2.5038),
    FIX8(0.2964), FIX8(2.4286), FIX8(0.3142), FIX8(2.3534),
    FIX8(0.3331), FIX8(2.2782), FIX8(0.3532), FIX8(2.2030),
    FIX8(0.3748), FIX8(2.1278), FIX8(0.3979), FIX8(2.0527),
    FIX8(0.4226), FIX8(1.9775), FIX8(0.4491), FIX8(1.9023),
    FIX8(0.4776), FIX8(1.8271), FIX8(0.5082), FIX8(1.7519),
    FIX8(0.5412), FIX8(1.6767), FIX8(0.5768), FIX8(1.6015),
    FIX8(0.6152), FIX8(1.5263), FIX8(0.6568), FIX8(1.4511),
    FIX8(0.7020), FIX8(1.3759), FIX8(0.7513), FIX8(1.3008),
    FIX8(0.8050), FIX8(1.2256), FIX8(0.8638), FIX8(1.1504),
    FIX8(0.9285), FIX8(1.0752), FIX8(1.0000), FIX8(1.0000)
};
const uint8_t x264_cabac_transition[128][2] =
{
    {  0,   0}, {  1,   1}, {  2,  50}, { 51,   3}, {  2,  50}, { 51,   3}, {  4,  52}, { 53,   5},
    {  6,  52}, { 53,   7}, {  8,  52}, { 53,   9}, { 10,  54}, { 55,  11}, { 12,  54}, { 55,  13},
    { 14,  54}, { 55,  15}, { 16,  56}, { 57,  17}, { 18,  56}, { 57,  19}, { 20,  56}, { 57,  21},
    { 22,  58}, { 59,  23}, { 24,  58}, { 59,  25}, { 26,  60}, { 61,  27}, { 28,  60}, { 61,  29},
    { 30,  60}, { 61,  31}, { 32,  62}, { 63,  33}, { 34,  62}, { 63,  35}, { 36,  64}, { 65,  37},
    { 38,  66}, { 67,  39}, { 40,  66}, { 67,  41}, { 42,  66}, { 67,  43}, { 44,  68}, { 69,  45},
    { 46,  68}, { 69,  47}, { 48,  70}, { 71,  49}, { 50,  72}, { 73,  51}, { 52,  72}, { 73,  53},
    { 54,  74}, { 75,  55}, { 56,  74}, { 75,  57}, { 58,  76}, { 77,  59}, { 60,  78}, { 79,  61},
    { 62,  78}, { 79,  63}, { 64,  80}, { 81,  65}, { 66,  82}, { 83,  67}, { 68,  82}, { 83,  69},
    { 70,  84}, { 85,  71}, { 72,  84}, { 85,  73}, { 74,  88}, { 89,  75}, { 76,  88}, { 89,  77},
    { 78,  90}, { 91,  79}, { 80,  90}, { 91,  81}, { 82,  94}, { 95,  83}, { 84,  94}, { 95,  85},
    { 86,  96}, { 97,  87}, { 88,  96}, { 97,  89}, { 90, 100}, {101,  91}, { 92, 100}, {101,  93},
    { 94, 102}, {103,  95}, { 96, 104}, {105,  97}, { 98, 104}, {105,  99}, {100, 108}, {109, 101},
    {102, 108}, {109, 103}, {104, 110}, {111, 105}, {106, 112}, {113, 107}, {108, 114}, {115, 109},
    {110, 116}, {117, 111}, {112, 118}, {119, 113}, {114, 118}, {119, 115}, {116, 122}, {123, 117},
    {118, 122}, {123, 119}, {120, 124}, {125, 121}, {122, 126}, {127, 123}, {124, 127}, {126, 125}
};
enum slice_type_e
{
    SLICE_TYPE_P  = 0,
    SLICE_TYPE_B  = 1,
    SLICE_TYPE_I  = 2,
};

typedef struct
{
    /* state */
    int i_low;
    int i_range;

    /* bit stream */
    int i_queue; //stored with an offset of -8 for faster asm
    int i_bytes_outstanding;

    uint8_t *p_start;
    uint8_t *p;
    uint8_t *p_end;

    /* aligned for memcpy_aligned starting here */
    ALIGNED_64( int f8_bits_encoded ); // only if using x264_cabac_size_decision()

    /* context */
    uint8_t state[1024];

    /* for 16-byte alignment */
    uint8_t padding[12];
} x264_cabac_t;

static ALWAYS_INLINE int x264_clip3( int v, int i_min, int i_max )
{
    return ( (v < i_min) ? i_min : (v > i_max) ? i_max : v );
}

static ALWAYS_INLINE double x264_clip3f( double v, double f_min, double f_max )
{
    return ( (v < f_min) ? f_min : (v > f_max) ? f_max : v );
}


/* init the contexts given i_slice_type, the quantif and the model */

void x264_cabac_context_init(  x264_cabac_t *cb, int i_slice_type, int i_qp, int i_model );

void x264_cabac_encode_init_core(x264_cabac_t*cb);
void x264_cabac_encode_init(x264_cabac_t*cb,uint8_t*p_data,uint8_t*p_end);
void x264_cabac_encode_decision_c(x264_cabac_t*cb,int i_ctx,int b);
void x264_cabac_encode_decision_asm(x264_cabac_t*cb,int i_ctx,int b);
void x264_cabac_encode_bypass_c(x264_cabac_t*cb,int b);
void x264_cabac_encode_bypass_asm(x264_cabac_t*cb,int b);
void x264_cabac_encode_terminal_c(x264_cabac_t*cb);
void x264_cabac_encode_terminal_asm(x264_cabac_t*cb);
void x264_cabac_encode_ue_bypass(x264_cabac_t*cb,int exp_bits,int val);
void x264_cabac_encode_flush(int frameNb,x264_cabac_t*cb);


#if HAVE_MMX
#define x264_cabac_encode_decision x264_cabac_encode_decision_asm
#define x264_cabac_encode_bypass x264_cabac_encode_bypass_asm
#define x264_cabac_encode_terminal x264_cabac_encode_terminal_asm
#elif HAVE_AARCH64
#define x264_cabac_encode_decision x264_cabac_encode_decision_asm
#define x264_cabac_encode_bypass x264_cabac_encode_bypass_asm
#define x264_cabac_encode_terminal x264_cabac_encode_terminal_asm
#else
#define x264_cabac_encode_decision x264_cabac_encode_decision_c
#define x264_cabac_encode_bypass x264_cabac_encode_bypass_c
#define x264_cabac_encode_terminal x264_cabac_encode_terminal_c
#endif
#define x264_cabac_encode_decision_noup x264_cabac_encode_decision

static ALWAYS_INLINE int x264_cabac_pos( x264_cabac_t *cb )
{
    return (cb->p - cb->p_start + cb->i_bytes_outstanding) * 8 + cb->i_queue;
}

/* internal only. these don't write the bitstream, just calculate bit cost: */

static ALWAYS_INLINE void x264_cabac_size_decision( x264_cabac_t *cb, long i_ctx, long b )
{
    int i_state = cb->state[i_ctx];
    cb->state[i_ctx] = x264_cabac_transition[i_state][b];
    cb->f8_bits_encoded += x264_cabac_entropy[i_state^b];
}

static ALWAYS_INLINE int x264_cabac_size_decision2( uint8_t *state, long b )
{
    int i_state = *state;
    *state = x264_cabac_transition[i_state][b];
    return x264_cabac_entropy[i_state^b];
}

static ALWAYS_INLINE void x264_cabac_size_decision_noup( x264_cabac_t *cb, long i_ctx, long b )
{
    int i_state = cb->state[i_ctx];
    cb->f8_bits_encoded += x264_cabac_entropy[i_state^b];
}

static ALWAYS_INLINE int x264_cabac_size_decision_noup2( uint8_t *state, long b )
{
    return x264_cabac_entropy[*state^b];
}
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
  #define x264_clz(x) __builtin_clz(x)
  #define x264_ctz(x) __builtin_ctz(x)
#else
  // Provide alternative definitions if needed for non-GNUC compilers.
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif  // X264_CABAC_H