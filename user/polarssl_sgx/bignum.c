/*
 *  Multi-precision integer library
 *
 *  Copyright (C) 2006-2014, Brainspark B.V.
 *
 *  This file is part of PolarSSL (http://www.polarssl.org)
 *  Lead Maintainer: Paul Bakker <polarssl_maintainer at polarssl.org>
 *
 *  All rights reserved.
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
 */
/*
 *  This MPI implementation is based on:
 *
 *  http://www.cacr.math.uwaterloo.ca/hac/about/chap14.pdf
 *  http://www.stillhq.com/extracted/gnupg-api/mpi/
 *  http://math.libtomcrypt.com/files/tommath.pdf
 */

#if !defined(POLARSSL_CONFIG_FILE)
#include "polarssl/config.h"
#else
#include POLARSSL_CONFIG_FILE
#endif

#if defined(POLARSSL_BIGNUM_C)

#include "polarssl/bignum.h"
#include "polarssl/bn_mul.h"
#include <stdlib.h>

#if defined(POLARSSL_PLATFORM_C)
#include "polarssl/platform.h"
#else
#define polarssl_printf     sgx_printf
#define polarssl_malloc     sgx_malloc
#define polarssl_free       sgx_free
#endif

#include "polarssl/ctr_drbg.h"

/* Implementation that should never be optimized out by the compiler */
static void sgx_polarssl_zeroize( void *v, size_t n ) {
    volatile unsigned char *p = v; while( n-- ) *p++ = 0;
}

#define ciL    (sizeof(t_uint))         /* chars in limb  */
#define biL    (ciL << 3)               /* bits  in limb  */
#define biH    (ciL << 2)               /* half limb size */

/*
 * Convert between bits/chars and number of limbs
 */
#define BITS_TO_LIMBS(i)  (((i) + biL - 1) / biL)
#define CHARS_TO_LIMBS(i) (((i) + ciL - 1) / ciL)

/*
 * Initialize one MPI
 */
void sgx_mpi_init( mpi *X )
{
    if( X == NULL )
        return;

    X->s = 1;
    X->n = 0;
    X->p = NULL;
}

/*
 * Unallocate one MPI
 */
void sgx_mpi_free( mpi *X )
{
    if( X == NULL )
        return;

    if( X->p != NULL )
    {
        sgx_polarssl_zeroize( X->p, X->n * ciL );
        polarssl_free( X->p );
    }

    X->s = 1;
    X->n = 0;
    X->p = NULL;
}

/*
 * Enlarge to the specified number of limbs
 */
int sgx_mpi_grow( mpi *X, size_t nblimbs )
{
    t_uint *p;

    if( nblimbs > POLARSSL_MPI_MAX_LIMBS )
        return( POLARSSL_ERR_MPI_MALLOC_FAILED );

    if( X->n < nblimbs )
    {
        if( ( p = (t_uint *) polarssl_malloc( nblimbs * ciL ) ) == NULL )
            return( POLARSSL_ERR_MPI_MALLOC_FAILED );

        sgx_memset( p, 0, nblimbs * ciL );

        if( X->p != NULL )
        {
            sgx_memcpy( p, X->p, X->n * ciL );
            sgx_polarssl_zeroize( X->p, X->n * ciL );
            polarssl_free( X->p );
        }

        X->n = nblimbs;
        X->p = p;
    }

    return( 0 );
}

#if 0
/*
 * Resize down as much as possible,
 * while keeping at least the specified number of limbs
 */
int mpi_shrink( mpi *X, size_t nblimbs )
{
    t_uint *p;
    size_t i;

    /* Actually resize up in this case */
    if( X->n <= nblimbs )
        return( mpi_grow( X, nblimbs ) );

    for( i = X->n - 1; i > 0; i-- )
        if( X->p[i] != 0 )
            break;
    i++;

    if( i < nblimbs )
        i = nblimbs;

    if( ( p = (t_uint *) polarssl_malloc( i * ciL ) ) == NULL )
        return( POLARSSL_ERR_MPI_MALLOC_FAILED );

    sgx_memset( p, 0, i * ciL );

    if( X->p != NULL )
    {
        sgx_memcpy( p, X->p, i * ciL );
        polarssl_zeroize( X->p, X->n * ciL );
        polarssl_free( X->p );
    }

    X->n = i;
    X->p = p;

    return( 0 );
}
#endif

/*
 * Copy the contents of Y into X
 */
int sgx_mpi_copy( mpi *X, const mpi *Y )
{
    int ret;
    size_t i;

    if( X == Y )
        return( 0 );

    if( Y->p == NULL )
    {
        sgx_mpi_free( X );
        return( 0 );
    }

    for( i = Y->n - 1; i > 0; i-- )
        if( Y->p[i] != 0 )
            break;
    i++;

    X->s = Y->s;

    MPI_CHK( sgx_mpi_grow( X, i ) );

    sgx_memset( X->p, 0, X->n * ciL );
    sgx_memcpy( X->p, Y->p, i * ciL );

cleanup:

    return( ret );
}

/*
 * Swap the contents of X and Y
 */
void sgx_mpi_swap( mpi *X, mpi *Y )
{
    mpi T;

    sgx_memcpy( &T,  X, sizeof( mpi ) );
    sgx_memcpy(  X,  Y, sizeof( mpi ) );
    sgx_memcpy(  Y, &T, sizeof( mpi ) );
}

#if 0
/*
 * Conditionally assign X = Y, without leaking information
 * about whether the assignment was made or not.
 * (Leaking information about the respective sizes of X and Y is ok however.)
 */
int mpi_safe_cond_assign( mpi *X, const mpi *Y, unsigned char assign )
{
    int ret = 0;
    size_t i;

    /* make sure assign is 0 or 1 */
    assign = ( assign != 0 );

    MPI_CHK( mpi_grow( X, Y->n ) );

    X->s = X->s * ( 1 - assign ) + Y->s * assign;

    for( i = 0; i < Y->n; i++ )
        X->p[i] = X->p[i] * ( 1 - assign ) + Y->p[i] * assign;

    for( ; i < X->n; i++ )
        X->p[i] *= ( 1 - assign );

cleanup:
    return( ret );
}

/*
 * Conditionally swap X and Y, without leaking information
 * about whether the swap was made or not.
 * Here it is not ok to simply swap the pointers, which whould lead to
 * different memory access patterns when X and Y are used afterwards.
 */
int mpi_safe_cond_swap( mpi *X, mpi *Y, unsigned char swap )
{
    int ret, s;
    size_t i;
    t_uint tmp;

    if( X == Y )
        return( 0 );

    /* make sure swap is 0 or 1 */
    swap = ( swap != 0 );

    MPI_CHK( mpi_grow( X, Y->n ) );
    MPI_CHK( mpi_grow( Y, X->n ) );

    s = X->s;
    X->s = X->s * ( 1 - swap ) + Y->s * swap;
    Y->s = Y->s * ( 1 - swap ) +    s * swap;


    for( i = 0; i < X->n; i++ )
    {
        tmp = X->p[i];
        X->p[i] = X->p[i] * ( 1 - swap ) + Y->p[i] * swap;
        Y->p[i] = Y->p[i] * ( 1 - swap ) +     tmp * swap;
    }

cleanup:
    return( ret );
}
#endif

/*
 * Set value from integer
 */
int sgx_mpi_lset( mpi *X, t_sint z )
{
    int ret;

    MPI_CHK( sgx_mpi_grow( X, 1 ) );
    sgx_memset( X->p, 0, X->n * ciL );

    X->p[0] = ( z < 0 ) ? -z : z;
    X->s    = ( z < 0 ) ? -1 : 1;

cleanup:

    return( ret );
}

#if 0
/*
 * Get a specific bit
 */
int mpi_get_bit( const mpi *X, size_t pos )
{
    if( X->n * biL <= pos )
        return( 0 );

    return( ( X->p[pos / biL] >> ( pos % biL ) ) & 0x01 );
}

/*
 * Set a bit to a specific value of 0 or 1
 */
int mpi_set_bit( mpi *X, size_t pos, unsigned char val )
{
    int ret = 0;
    size_t off = pos / biL;
    size_t idx = pos % biL;

    if( val != 0 && val != 1 )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    if( X->n * biL <= pos )
    {
        if( val == 0 )
            return( 0 );

        MPI_CHK( mpi_grow( X, off + 1 ) );
    }

    X->p[off] &= ~( (t_uint) 0x01 << idx );
    X->p[off] |= (t_uint) val << idx;

cleanup:

    return( ret );
}
#endif

/*
 * Return the number of least significant bits
 */
size_t sgx_mpi_lsb( const mpi *X )
{
    size_t i, j, count = 0;

    for( i = 0; i < X->n; i++ )
        for( j = 0; j < biL; j++, count++ )
            if( ( ( X->p[i] >> j ) & 1 ) != 0 )
                return( count );

    return( 0 );
}

/*
 * Return the number of most significant bits
 */
size_t sgx_mpi_msb( const mpi *X )
{
    size_t i, j;

    for( i = X->n - 1; i > 0; i-- )
        if( X->p[i] != 0 )
            break;

    for( j = biL; j > 0; j-- )
        if( ( ( X->p[i] >> ( j - 1 ) ) & 1 ) != 0 )
            break;

    return( ( i * biL ) + j );
}

/*
 * Return the total size in bytes
 */
size_t sgx_mpi_size( const mpi *X )
{
    return( ( sgx_mpi_msb( X ) + 7 ) >> 3 );
}

/*
 * Convert an ASCII character to digit value
 */
static int sgx_mpi_get_digit( t_uint *d, int radix, char c )
{
    *d = 255;

    if( c >= 0x30 && c <= 0x39 ) *d = c - 0x30;
    if( c >= 0x41 && c <= 0x46 ) *d = c - 0x37;
    if( c >= 0x61 && c <= 0x66 ) *d = c - 0x57;

    if( *d >= (t_uint) radix )
        return( POLARSSL_ERR_MPI_INVALID_CHARACTER );

    return( 0 );
}

/*
 * Import from an ASCII string
 */
int sgx_mpi_read_string( mpi *X, int radix, const char *s )
{
    int ret;
    size_t i, j, slen, n;
    t_uint d;
    mpi T;

    if( radix < 2 || radix > 16 )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    sgx_mpi_init( &T );

    slen = sgx_strlen( s );

    if( radix == 16 )
    {
        n = BITS_TO_LIMBS( slen << 2 );

        MPI_CHK( sgx_mpi_grow( X, n ) );
        MPI_CHK( sgx_mpi_lset( X, 0 ) );

        for( i = slen, j = 0; i > 0; i--, j++ )
        {
            if( i == 1 && s[i - 1] == '-' )
            {
                X->s = -1;
                break;
            }

            MPI_CHK( sgx_mpi_get_digit( &d, radix, s[i - 1] ) );
            X->p[j / ( 2 * ciL )] |= d << ( ( j % ( 2 * ciL ) ) << 2 );
        }
    }
    else
    {
        MPI_CHK( sgx_mpi_lset( X, 0 ) );

        for( i = 0; i < slen; i++ )
        {
            if( i == 0 && s[i] == '-' )
            {
                X->s = -1;
                continue;
            }

            MPI_CHK( sgx_mpi_get_digit( &d, radix, s[i] ) );
            MPI_CHK( sgx_mpi_mul_int( &T, X, radix ) );

            if( X->s == 1 )
            {
                MPI_CHK( sgx_mpi_add_int( X, &T, d ) );
            }
            else
            {
                MPI_CHK( sgx_mpi_sub_int( X, &T, d ) );
            }
        }
    }
cleanup:

    sgx_mpi_free( &T );

    return( ret );
}

/*
 * Helper to write the digits high-order first
 */
static int sgx_mpi_write_hlp( mpi *X, int radix, char **p )
{
    int ret;
    t_uint r;

    if( radix < 2 || radix > 16 )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    MPI_CHK( sgx_mpi_mod_int( &r, X, radix ) );
    MPI_CHK( sgx_mpi_div_int( X, NULL, X, radix ) );

    if( sgx_mpi_cmp_int( X, 0 ) != 0 )
        MPI_CHK( sgx_mpi_write_hlp( X, radix, p ) );

    if( r < 10 )
        *(*p)++ = (char)( r + 0x30 );
    else
        *(*p)++ = (char)( r + 0x37 );

cleanup:

    return( ret );
}

/*
 * Export into an ASCII string
 */
#if 0
int sgx_mpi_write_string( const mpi *X, int radix, char *s, size_t *slen )
{
    int ret = 0;
    size_t n;
    char *p;
    mpi T;

    if( radix < 2 || radix > 16 )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    n = sgx_mpi_msb( X );
    if( radix >=  4 ) n >>= 1;
    if( radix >= 16 ) n >>= 1;
    n += 3;

    if( *slen < n )
    {
        *slen = n;
        return( POLARSSL_ERR_MPI_BUFFER_TOO_SMALL );
    }

    p = s;
    sgx_mpi_init( &T );

    if( X->s == -1 )
        *p++ = '-';

    if( radix == 16 )
    {
        int c;
        size_t i, j, k;

        for( i = X->n, k = 0; i > 0; i-- )
        {
            for( j = ciL; j > 0; j-- )
            {
                c = ( X->p[i - 1] >> ( ( j - 1 ) << 3) ) & 0xFF;

                if( c == 0 && k == 0 && ( i + j ) != 2 )
                    continue;

                *(p++) = "0123456789ABCDEF" [c / 16];
                *(p++) = "0123456789ABCDEF" [c % 16];
                k = 1;
            }
        }
    }
    else
    {
        MPI_CHK( sgx_mpi_copy( &T, X ) );

        if( T.s == -1 )
            T.s = 1;

        MPI_CHK( sgx_mpi_write_hlp( &T, radix, &p ) );
    }

    *p++ = '\0';
    *slen = p - s;

cleanup:

    sgx_mpi_free( &T );

    return( ret );
}

#if defined(POLARSSL_FS_IO)
/*
 * Read X from an opened file
 */
int mpi_read_file( mpi *X, int radix, FILE *fin )
{
    t_uint d;
    size_t slen;
    char *p;
    /*
     * Buffer should have space for (short) label and decimal formatted MPI,
     * newline characters and '\0'
     */
    char s[ POLARSSL_MPI_RW_BUFFER_SIZE ];

    sgx_memset( s, 0, sizeof( s ) );
    if( fgets( s, sizeof( s ) - 1, fin ) == NULL )
        return( POLARSSL_ERR_MPI_FILE_IO_ERROR );

    slen = sgx_strlen( s );
    if( slen == sizeof( s ) - 2 )
        return( POLARSSL_ERR_MPI_BUFFER_TOO_SMALL );

    if( s[slen - 1] == '\n' ) { slen--; s[slen] = '\0'; }
    if( s[slen - 1] == '\r' ) { slen--; s[slen] = '\0'; }

    p = s + slen;
    while( --p >= s )
        if( mpi_get_digit( &d, radix, *p ) != 0 )
            break;

    return( mpi_read_string( X, radix, p + 1 ) );
}

/*
 * Write X into an opened file (or stdout if fout == NULL)
 */
int mpi_write_file( const char *p, const mpi *X, int radix, FILE *fout )
{
    int ret;
    size_t n, slen, plen;
    /*
     * Buffer should have space for (short) label and decimal formatted MPI,
     * newline characters and '\0'
     */
    char s[ POLARSSL_MPI_RW_BUFFER_SIZE ];

    n = sizeof( s );
    sgx_memset( s, 0, n );
    n -= 2;

    MPI_CHK( mpi_write_string( X, radix, s, (size_t *) &n ) );

    if( p == NULL ) p = "";

    plen = sgx_strlen( p );
    slen = sgx_strlen( s );
    s[slen++] = '\r';
    s[slen++] = '\n';

    if( fout != NULL )
    {
        if( fwrite( p, 1, plen, fout ) != plen ||
            fwrite( s, 1, slen, fout ) != slen )
            return( POLARSSL_ERR_MPI_FILE_IO_ERROR );
    }
    else
        polarssl_printf( "%s%s", p, s );

cleanup:

    return( ret );
}
#endif /* POLARSSL_FS_IO */
#endif

/*
 * Import X from unsigned binary data, big endian
 */
int sgx_mpi_read_binary( mpi *X, const unsigned char *buf, size_t buflen )
{
    int ret;
    size_t i, j, n;

    for( n = 0; n < buflen; n++ )
        if( buf[n] != 0 )
            break;

    MPI_CHK( sgx_mpi_grow( X, CHARS_TO_LIMBS( buflen - n ) ) );
    MPI_CHK( sgx_mpi_lset( X, 0 ) );

    for( i = buflen, j = 0; i > n; i--, j++ )
        X->p[j / ciL] |= ((t_uint) buf[i - 1]) << ((j % ciL) << 3);

cleanup:

    return( ret );
}

/*
 * Export X into unsigned binary data, big endian
 */
int sgx_mpi_write_binary( const mpi *X, unsigned char *buf, size_t buflen )
{
    size_t i, j, n;

    n = sgx_mpi_size( X );

    if( buflen < n )
        return( POLARSSL_ERR_MPI_BUFFER_TOO_SMALL );

    sgx_memset( buf, 0, buflen );

    for( i = buflen - 1, j = 0; n > 0; i--, j++, n-- )
        buf[i] = (unsigned char)( X->p[j / ciL] >> ((j % ciL) << 3) );

    return( 0 );
}

/*
 * Left-shift: X <<= count
 */
int sgx_mpi_shift_l( mpi *X, size_t count )
{
    int ret;
    size_t i, v0, t1;
    t_uint r0 = 0, r1;

    v0 = count / (biL    );
    t1 = count & (biL - 1);

    i = sgx_mpi_msb( X ) + count;

    if( X->n * biL < i )
        MPI_CHK( sgx_mpi_grow( X, BITS_TO_LIMBS( i ) ) );

    ret = 0;

    /*
     * shift by count / limb_size
     */
    if( v0 > 0 )
    {
        for( i = X->n; i > v0; i-- )
            X->p[i - 1] = X->p[i - v0 - 1];

        for( ; i > 0; i-- )
            X->p[i - 1] = 0;
    }

    /*
     * shift by count % limb_size
     */
    if( t1 > 0 )
    {
        for( i = v0; i < X->n; i++ )
        {
            r1 = X->p[i] >> (biL - t1);
            X->p[i] <<= t1;
            X->p[i] |= r0;
            r0 = r1;
        }
    }

cleanup:

    return( ret );
}

/*
 * Right-shift: X >>= count
 */
int sgx_mpi_shift_r( mpi *X, size_t count )
{
    size_t i, v0, v1;
    t_uint r0 = 0, r1;

    v0 = count /  biL;
    v1 = count & (biL - 1);

    if( v0 > X->n || ( v0 == X->n && v1 > 0 ) )
        return sgx_mpi_lset( X, 0 );

    /*
     * shift by count / limb_size
     */
    if( v0 > 0 )
    {
        for( i = 0; i < X->n - v0; i++ )
            X->p[i] = X->p[i + v0];

        for( ; i < X->n; i++ )
            X->p[i] = 0;
    }

    /*
     * shift by count % limb_size
     */
    if( v1 > 0 )
    {
        for( i = X->n; i > 0; i-- )
        {
            r1 = X->p[i - 1] << (biL - v1);
            X->p[i - 1] >>= v1;
            X->p[i - 1] |= r0;
            r0 = r1;
        }
    }

    return( 0 );
}

/*
 * Compare unsigned values
 */
int sgx_mpi_cmp_abs( const mpi *X, const mpi *Y )
{
    size_t i, j;

    for( i = X->n; i > 0; i-- )
        if( X->p[i - 1] != 0 )
            break;

    for( j = Y->n; j > 0; j-- )
        if( Y->p[j - 1] != 0 )
            break;

    if( i == 0 && j == 0 )
        return( 0 );

    if( i > j ) return(  1 );
    if( j > i ) return( -1 );

    for( ; i > 0; i-- )
    {
        if( X->p[i - 1] > Y->p[i - 1] ) return(  1 );
        if( X->p[i - 1] < Y->p[i - 1] ) return( -1 );
    }

    return( 0 );
}

/*
 * Compare signed values
 */
int sgx_mpi_cmp_mpi( const mpi *X, const mpi *Y )
{
    size_t i, j;

    for( i = X->n; i > 0; i-- )
        if( X->p[i - 1] != 0 )
            break;

    for( j = Y->n; j > 0; j-- )
        if( Y->p[j - 1] != 0 )
            break;

    if( i == 0 && j == 0 )
        return( 0 );

    if( i > j ) return(  X->s );
    if( j > i ) return( -Y->s );

    if( X->s > 0 && Y->s < 0 ) return(  1 );
    if( Y->s > 0 && X->s < 0 ) return( -1 );

    for( ; i > 0; i-- )
    {
        if( X->p[i - 1] > Y->p[i - 1] ) return(  X->s );
        if( X->p[i - 1] < Y->p[i - 1] ) return( -X->s );
    }

    return( 0 );
}

/*
 * Compare signed values
 */
int sgx_mpi_cmp_int( const mpi *X, t_sint z )
{
    mpi Y;
    t_uint p[1];

    *p  = ( z < 0 ) ? -z : z;
    Y.s = ( z < 0 ) ? -1 : 1;
    Y.n = 1;
    Y.p = p;

    return( sgx_mpi_cmp_mpi( X, &Y ) );
}

/*
 * Unsigned addition: X = |A| + |B|  (HAC 14.7)
 */
int sgx_mpi_add_abs( mpi *X, const mpi *A, const mpi *B )
{
    int ret;
    size_t i, j;
    t_uint *o, *p, c;

    if( X == B )
    {
        const mpi *T = A; A = X; B = T;
    }

    if( X != A )
        MPI_CHK( sgx_mpi_copy( X, A ) );

    /*
     * X should always be positive as a result of unsigned additions.
     */
    X->s = 1;

    for( j = B->n; j > 0; j-- )
        if( B->p[j - 1] != 0 )
            break;

    MPI_CHK( sgx_mpi_grow( X, j ) );

    o = B->p; p = X->p; c = 0;

    for( i = 0; i < j; i++, o++, p++ )
    {
        *p +=  c; c  = ( *p <  c );
        *p += *o; c += ( *p < *o );
    }

    while( c != 0 )
    {
        if( i >= X->n )
        {
            MPI_CHK( sgx_mpi_grow( X, i + 1 ) );
            p = X->p + i;
        }

        *p += c; c = ( *p < c ); i++; p++;
    }

cleanup:

    return( ret );
}

/*
 * Helper for mpi subtraction
 */
static void sgx_mpi_sub_hlp( size_t n, t_uint *s, t_uint *d )
{
    size_t i;
    t_uint c, z;

    for( i = c = 0; i < n; i++, s++, d++ )
    {
        z = ( *d <  c );     *d -=  c;
        c = ( *d < *s ) + z; *d -= *s;
    }

    while( c != 0 )
    {
        z = ( *d < c ); *d -= c;
        c = z; i++; d++;
    }
}

/*
 * Unsigned subtraction: X = |A| - |B|  (HAC 14.9)
 */
int sgx_mpi_sub_abs( mpi *X, const mpi *A, const mpi *B )
{
    mpi TB;
    int ret;
    size_t n;

    if( sgx_mpi_cmp_abs( A, B ) < 0 )
        return( POLARSSL_ERR_MPI_NEGATIVE_VALUE );

    sgx_mpi_init( &TB );

    if( X == B )
    {
        MPI_CHK( sgx_mpi_copy( &TB, B ) );
        B = &TB;
    }

    if( X != A )
        MPI_CHK( sgx_mpi_copy( X, A ) );

    /*
     * X should always be positive as a result of unsigned subtractions.
     */
    X->s = 1;

    ret = 0;

    for( n = B->n; n > 0; n-- )
        if( B->p[n - 1] != 0 )
            break;

    sgx_mpi_sub_hlp( n, B->p, X->p );

cleanup:

    sgx_mpi_free( &TB );

    return( ret );
}

/*
 * Signed addition: X = A + B
 */
int sgx_mpi_add_mpi( mpi *X, const mpi *A, const mpi *B )
{
    int ret, s = A->s;

    if( A->s * B->s < 0 )
    {
        if( sgx_mpi_cmp_abs( A, B ) >= 0 )
        {
            MPI_CHK( sgx_mpi_sub_abs( X, A, B ) );
            X->s =  s;
        }
        else
        {
            MPI_CHK( sgx_mpi_sub_abs( X, B, A ) );
            X->s = -s;
        }
    }
    else
    {
        MPI_CHK( sgx_mpi_add_abs( X, A, B ) );
        X->s = s;
    }

cleanup:

    return( ret );
}

/*
 * Signed subtraction: X = A - B
 */
int sgx_mpi_sub_mpi( mpi *X, const mpi *A, const mpi *B )
{
    int ret, s = A->s;

    if( A->s * B->s > 0 )
    {
        if( sgx_mpi_cmp_abs( A, B ) >= 0 )
        {
            MPI_CHK( sgx_mpi_sub_abs( X, A, B ) );
            X->s =  s;
        }
        else
        {
            MPI_CHK( sgx_mpi_sub_abs( X, B, A ) );
            X->s = -s;
        }
    }
    else
    {
        MPI_CHK( sgx_mpi_add_abs( X, A, B ) );
        X->s = s;
    }

cleanup:

    return( ret );
}

/*
 * Signed addition: X = A + b
 */
int sgx_mpi_add_int( mpi *X, const mpi *A, t_sint b )
{
    mpi _B;
    t_uint p[1];

    p[0] = ( b < 0 ) ? -b : b;
    _B.s = ( b < 0 ) ? -1 : 1;
    _B.n = 1;
    _B.p = p;

    return( sgx_mpi_add_mpi( X, A, &_B ) );
}

/*
 * Signed subtraction: X = A - b
 */
int sgx_mpi_sub_int( mpi *X, const mpi *A, t_sint b )
{
    mpi _B;
    t_uint p[1];

    p[0] = ( b < 0 ) ? -b : b;
    _B.s = ( b < 0 ) ? -1 : 1;
    _B.n = 1;
    _B.p = p;

    return( sgx_mpi_sub_mpi( X, A, &_B ) );
}

/*
 * Helper for mpi multiplication
 */
static
#if defined(__APPLE__) && defined(__arm__)
/*
 * Apple LLVM version 4.2 (clang-425.0.24) (based on LLVM 3.2svn)
 * appears to need this to prevent bad ARM code generation at -O3.
 */
__attribute__ ((noinline))
#endif
void sgx_mpi_mul_hlp( size_t i, t_uint *s, t_uint *d, t_uint b )
{
    t_uint c = 0, t = 0;

#if defined(MULADDC_HUIT)
    for( ; i >= 8; i -= 8 )
    {
        MULADDC_INIT
        MULADDC_HUIT
        MULADDC_STOP
    }

    for( ; i > 0; i-- )
    {
        MULADDC_INIT
        MULADDC_CORE
        MULADDC_STOP
    }
#else /* MULADDC_HUIT */
    for( ; i >= 16; i -= 16 )
    {
        MULADDC_INIT
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE

        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_STOP
    }

    for( ; i >= 8; i -= 8 )
    {
        MULADDC_INIT
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE

        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_STOP
    }

    for( ; i > 0; i-- )
    {
        MULADDC_INIT
        MULADDC_CORE
        MULADDC_STOP
    }
#endif /* MULADDC_HUIT */

    t++;

    do {
        *d += c; c = ( *d < c ); d++;
    }
    while( c != 0 );
}

/*
 * Baseline multiplication: X = A * B  (HAC 14.12)
 */
int sgx_mpi_mul_mpi( mpi *X, const mpi *A, const mpi *B )
{
    int ret;
    size_t i, j;
    mpi TA, TB;

    sgx_mpi_init( &TA ); sgx_mpi_init( &TB );

    if( X == A ) { MPI_CHK( sgx_mpi_copy( &TA, A ) ); A = &TA; }
    if( X == B ) { MPI_CHK( sgx_mpi_copy( &TB, B ) ); B = &TB; }

    for( i = A->n; i > 0; i-- )
        if( A->p[i - 1] != 0 )
            break;

    for( j = B->n; j > 0; j-- )
        if( B->p[j - 1] != 0 )
            break;

    MPI_CHK( sgx_mpi_grow( X, i + j ) );
    MPI_CHK( sgx_mpi_lset( X, 0 ) );

    for( i++; j > 0; j-- )
        sgx_mpi_mul_hlp( i - 1, A->p, X->p + j - 1, B->p[j - 1] );

    X->s = A->s * B->s;

cleanup:

    sgx_mpi_free( &TB ); sgx_mpi_free( &TA );

    return( ret );
}

/*
 * Baseline multiplication: X = A * b
 */
int sgx_mpi_mul_int( mpi *X, const mpi *A, t_sint b )
{
    mpi _B;
    t_uint p[1];

    _B.s = 1;
    _B.n = 1;
    _B.p = p;
    p[0] = b;

    return( sgx_mpi_mul_mpi( X, A, &_B ) );
}

/*
 * Division by mpi: A = Q * B + R  (HAC 14.20)
 */
int sgx_mpi_div_mpi( mpi *Q, mpi *R, const mpi *A, const mpi *B )
{
    int ret;
    size_t i, n, t, k;
    mpi X, Y, Z, T1, T2;

    if( sgx_mpi_cmp_int( B, 0 ) == 0 )
        return( POLARSSL_ERR_MPI_DIVISION_BY_ZERO );

    sgx_mpi_init( &X ); sgx_mpi_init( &Y ); sgx_mpi_init( &Z );
    sgx_mpi_init( &T1 ); sgx_mpi_init( &T2 );

    if( sgx_mpi_cmp_abs( A, B ) < 0 )
    {
        if( Q != NULL ) MPI_CHK( sgx_mpi_lset( Q, 0 ) );
        if( R != NULL ) MPI_CHK( sgx_mpi_copy( R, A ) );
        return( 0 );
    }

    MPI_CHK( sgx_mpi_copy( &X, A ) );
    MPI_CHK( sgx_mpi_copy( &Y, B ) );
    X.s = Y.s = 1;

    MPI_CHK( sgx_mpi_grow( &Z, A->n + 2 ) );
    MPI_CHK( sgx_mpi_lset( &Z,  0 ) );
    MPI_CHK( sgx_mpi_grow( &T1, 2 ) );
    MPI_CHK( sgx_mpi_grow( &T2, 3 ) );

    k = sgx_mpi_msb( &Y ) % biL;
    if( k < biL - 1 )
    {
        k = biL - 1 - k;
        MPI_CHK( sgx_mpi_shift_l( &X, k ) );
        MPI_CHK( sgx_mpi_shift_l( &Y, k ) );
    }
    else k = 0;

    n = X.n - 1;
    t = Y.n - 1;
    MPI_CHK( sgx_mpi_shift_l( &Y, biL * ( n - t ) ) );

    while( sgx_mpi_cmp_mpi( &X, &Y ) >= 0 )
    {
        Z.p[n - t]++;
        MPI_CHK( sgx_mpi_sub_mpi( &X, &X, &Y ) );
    }
    MPI_CHK( sgx_mpi_shift_r( &Y, biL * ( n - t ) ) );

    for( i = n; i > t ; i-- )
    {
        if( X.p[i] >= Y.p[t] )
            Z.p[i - t - 1] = ~0;
        else
        {
            /*
             * The version of Clang shipped by Apple with Mavericks around
             * 2014-03 can't handle 128-bit division properly. Disable
             * 128-bits division for this version. Let's be optimistic and
             * assume it'll be fixed in the next minor version (next
             * patchlevel is probably a bit too optimistic).
             */
#if defined(POLARSSL_HAVE_UDBL) &&                          \
    ! ( defined(__x86_64__) && defined(__APPLE__) &&        \
        defined(__clang_major__) && __clang_major__ == 5 && \
        defined(__clang_minor__) && __clang_minor__ == 0 )
#if 0
            t_udbl r;

            r  = (t_udbl) X.p[i] << biL;
            r |= (t_udbl) X.p[i - 1];
            r /= Y.p[t];
            if( r > ( (t_udbl) 1 << biL ) - 1 )
                r = ( (t_udbl) 1 << biL ) - 1;

            Z.p[i - t - 1] = (t_uint) r;
#endif
//#else
            /*
             * __udiv_qrnnd_c, from gmp/longlong.h
             */
            t_uint q0, q1, r0, r1;
            t_uint d0, d1, d, m;

            d  = Y.p[t];
            d0 = ( d << biH ) >> biH;
            d1 = ( d >> biH );

            q1 = X.p[i] / d1;
            r1 = X.p[i] - d1 * q1;
            r1 <<= biH;
            r1 |= ( X.p[i - 1] >> biH );

            m = q1 * d0;
            if( r1 < m )
            {
                q1--, r1 += d;
                while( r1 >= d && r1 < m )
                    q1--, r1 += d;
            }
            r1 -= m;

            q0 = r1 / d1;
            r0 = r1 - d1 * q0;
            r0 <<= biH;
            r0 |= ( X.p[i - 1] << biH ) >> biH;

            m = q0 * d0;
            if( r0 < m )
            {
                q0--, r0 += d;
                while( r0 >= d && r0 < m )
                    q0--, r0 += d;
            }
            r0 -= m;

            Z.p[i - t - 1] = ( q1 << biH ) | q0;
#endif /* POLARSSL_HAVE_UDBL && !64-bit Apple with Clang 5.0 */
        }

        Z.p[i - t - 1]++;
        do
        {
            Z.p[i - t - 1]--;

            MPI_CHK( sgx_mpi_lset( &T1, 0 ) );
            T1.p[0] = ( t < 1 ) ? 0 : Y.p[t - 1];
            T1.p[1] = Y.p[t];
            MPI_CHK( sgx_mpi_mul_int( &T1, &T1, Z.p[i - t - 1] ) );

            MPI_CHK( sgx_mpi_lset( &T2, 0 ) );
            T2.p[0] = ( i < 2 ) ? 0 : X.p[i - 2];
            T2.p[1] = ( i < 1 ) ? 0 : X.p[i - 1];
            T2.p[2] = X.p[i];
        }
        while( sgx_mpi_cmp_mpi( &T1, &T2 ) > 0 );

        MPI_CHK( sgx_mpi_mul_int( &T1, &Y, Z.p[i - t - 1] ) );
        MPI_CHK( sgx_mpi_shift_l( &T1,  biL * ( i - t - 1 ) ) );
        MPI_CHK( sgx_mpi_sub_mpi( &X, &X, &T1 ) );

        if( sgx_mpi_cmp_int( &X, 0 ) < 0 )
        {
            MPI_CHK( sgx_mpi_copy( &T1, &Y ) );
            MPI_CHK( sgx_mpi_shift_l( &T1, biL * ( i - t - 1 ) ) );
            MPI_CHK( sgx_mpi_add_mpi( &X, &X, &T1 ) );
            Z.p[i - t - 1]--;
        }
    }

    if( Q != NULL )
    {
        MPI_CHK( sgx_mpi_copy( Q, &Z ) );
        Q->s = A->s * B->s;
    }

    if( R != NULL )
    {
        MPI_CHK( sgx_mpi_shift_r( &X, k ) );
        X.s = A->s;
        MPI_CHK( sgx_mpi_copy( R, &X ) );

        if( sgx_mpi_cmp_int( R, 0 ) == 0 )
            R->s = 1;
    }

cleanup:

    sgx_mpi_free( &X ); sgx_mpi_free( &Y ); sgx_mpi_free( &Z );
    sgx_mpi_free( &T1 ); sgx_mpi_free( &T2 );

    return( ret );
}

/*
 * Division by int: A = Q * b + R
 */
int sgx_mpi_div_int( mpi *Q, mpi *R, const mpi *A, t_sint b )
{
    mpi _B;
    t_uint p[1];

    p[0] = ( b < 0 ) ? -b : b;
    _B.s = ( b < 0 ) ? -1 : 1;
    _B.n = 1;
    _B.p = p;

    return( sgx_mpi_div_mpi( Q, R, A, &_B ) );
}

/*
 * Modulo: R = A mod B
 */
int sgx_mpi_mod_mpi( mpi *R, const mpi *A, const mpi *B )
{
    int ret;

    if( sgx_mpi_cmp_int( B, 0 ) < 0 )
        return( POLARSSL_ERR_MPI_NEGATIVE_VALUE );

    MPI_CHK( sgx_mpi_div_mpi( NULL, R, A, B ) );

    while( sgx_mpi_cmp_int( R, 0 ) < 0 )
      MPI_CHK( sgx_mpi_add_mpi( R, R, B ) );

    while( sgx_mpi_cmp_mpi( R, B ) >= 0 )
      MPI_CHK( sgx_mpi_sub_mpi( R, R, B ) );

cleanup:

    return( ret );
}

/*
 * Modulo: r = A mod b
 */
int sgx_mpi_mod_int( t_uint *r, const mpi *A, t_sint b )
{
    size_t i;
    t_uint x, y, z;

    if( b == 0 )
        return( POLARSSL_ERR_MPI_DIVISION_BY_ZERO );

    if( b < 0 )
        return( POLARSSL_ERR_MPI_NEGATIVE_VALUE );

    /*
     * handle trivial cases
     */
    if( b == 1 )
    {
        *r = 0;
        return( 0 );
    }

    if( b == 2 )
    {
        *r = A->p[0] & 1;
        return( 0 );
    }

    /*
     * general case
     */
    for( i = A->n, y = 0; i > 0; i-- )
    {
        x  = A->p[i - 1];
        y  = ( y << biH ) | ( x >> biH );
        z  = y / b;
        y -= z * b;

        x <<= biH;
        y  = ( y << biH ) | ( x >> biH );
        z  = y / b;
        y -= z * b;
    }

    /*
     * If A is negative, then the current y represents a negative value.
     * Flipping it to the positive side.
     */
    if( A->s < 0 && y != 0 )
        y = b - y;

    *r = y;

    return( 0 );
}

/*
 * Fast Montgomery initialization (thanks to Tom St Denis)
 */
static void sgx_mpi_montg_init( t_uint *mm, const mpi *N )
{
    t_uint x, m0 = N->p[0];
    unsigned int i;

    x  = m0;
    x += ( ( m0 + 2 ) & 4 ) << 1;

    for( i = biL; i >= 8; i /= 2 )
        x *= ( 2 - ( m0 * x ) );

    *mm = ~x + 1;
}

/*
 * Montgomery multiplication: A = A * B * R^-1 mod N  (HAC 14.36)
 */
static void sgx_mpi_montmul( mpi *A, const mpi *B, const mpi *N, t_uint mm,
                         const mpi *T )
{
    size_t i, n, m;
    t_uint u0, u1, *d;

    sgx_memset( T->p, 0, T->n * ciL );

    d = T->p;
    n = N->n;
    m = ( B->n < n ) ? B->n : n;

    for( i = 0; i < n; i++ )
    {
        /*
         * T = (T + u0*B + u1*N) / 2^biL
         */
        u0 = A->p[i];
        u1 = ( d[0] + u0 * B->p[0] ) * mm;

        sgx_mpi_mul_hlp( m, B->p, d, u0 );
        sgx_mpi_mul_hlp( n, N->p, d, u1 );

        *d++ = u0; d[n + 1] = 0;
    }

    sgx_memcpy( A->p, d, ( n + 1 ) * ciL );

    if( sgx_mpi_cmp_abs( A, N ) >= 0 )
        sgx_mpi_sub_hlp( n, N->p, A->p );
    else
        /* prevent timing attacks */
        sgx_mpi_sub_hlp( n, A->p, T->p );
}

/*
 * Montgomery reduction: A = A * R^-1 mod N
 */
static void sgx_mpi_montred( mpi *A, const mpi *N, t_uint mm, const mpi *T )
{
    t_uint z = 1;
    mpi U;

    U.n = U.s = (int) z;
    U.p = &z;

    sgx_mpi_montmul( A, &U, N, mm, T );
}

/*
 * Sliding-window exponentiation: X = A^E mod N  (HAC 14.85)
 */
int sgx_mpi_exp_mod( mpi *X, const mpi *A, const mpi *E, const mpi *N, mpi *_RR )
{
    int ret;
    size_t wbits, wsize, one = 1;
    size_t i, j, nblimbs;
    size_t bufsize, nbits;
    t_uint ei, mm, state;
    mpi RR, T, W[ 2 << POLARSSL_MPI_WINDOW_SIZE ], Apos;
    int neg;

    if( sgx_mpi_cmp_int( N, 0 ) < 0 || ( N->p[0] & 1 ) == 0 )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    if( sgx_mpi_cmp_int( E, 0 ) < 0 )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    /*
     * Init temps and window size
     */
    sgx_mpi_montg_init( &mm, N );
    sgx_mpi_init( &RR ); sgx_mpi_init( &T );
    sgx_mpi_init( &Apos );
    sgx_memset( W, 0, sizeof( W ) );

    i = sgx_mpi_msb( E );

    wsize = ( i > 671 ) ? 6 : ( i > 239 ) ? 5 :
            ( i >  79 ) ? 4 : ( i >  23 ) ? 3 : 1;

    if( wsize > POLARSSL_MPI_WINDOW_SIZE )
        wsize = POLARSSL_MPI_WINDOW_SIZE;

    j = N->n + 1;
    MPI_CHK( sgx_mpi_grow( X, j ) );
    MPI_CHK( sgx_mpi_grow( &W[1],  j ) );
    MPI_CHK( sgx_mpi_grow( &T, j * 2 ) );

    /*
     * Compensate for negative A (and correct at the end)
     */
    neg = ( A->s == -1 );
    if( neg )
    {
        MPI_CHK( sgx_mpi_copy( &Apos, A ) );
        Apos.s = 1;
        A = &Apos;
    }

    /*
     * If 1st call, pre-compute R^2 mod N
     */
    if( _RR == NULL || _RR->p == NULL )
    {
        MPI_CHK( sgx_mpi_lset( &RR, 1 ) );
        MPI_CHK( sgx_mpi_shift_l( &RR, N->n * 2 * biL ) );
        MPI_CHK( sgx_mpi_mod_mpi( &RR, &RR, N ) );

        if( _RR != NULL )
            sgx_memcpy( _RR, &RR, sizeof( mpi ) );
    }
    else
        sgx_memcpy( &RR, _RR, sizeof( mpi ) );

    /*
     * W[1] = A * R^2 * R^-1 mod N = A * R mod N
     */
    if( sgx_mpi_cmp_mpi( A, N ) >= 0 )
        MPI_CHK( sgx_mpi_mod_mpi( &W[1], A, N ) );
    else
        MPI_CHK( sgx_mpi_copy( &W[1], A ) );

    sgx_mpi_montmul( &W[1], &RR, N, mm, &T );

    /*
     * X = R^2 * R^-1 mod N = R mod N
     */
    MPI_CHK( sgx_mpi_copy( X, &RR ) );
    sgx_mpi_montred( X, N, mm, &T );

    if( wsize > 1 )
    {
        /*
         * W[1 << (wsize - 1)] = W[1] ^ (wsize - 1)
         */
        j =  one << ( wsize - 1 );

        MPI_CHK( sgx_mpi_grow( &W[j], N->n + 1 ) );
        MPI_CHK( sgx_mpi_copy( &W[j], &W[1]    ) );

        for( i = 0; i < wsize - 1; i++ )
            sgx_mpi_montmul( &W[j], &W[j], N, mm, &T );

        /*
         * W[i] = W[i - 1] * W[1]
         */
        for( i = j + 1; i < ( one << wsize ); i++ )
        {
            MPI_CHK( sgx_mpi_grow( &W[i], N->n + 1 ) );
            MPI_CHK( sgx_mpi_copy( &W[i], &W[i - 1] ) );

            sgx_mpi_montmul( &W[i], &W[1], N, mm, &T );
        }
    }

    nblimbs = E->n;
    bufsize = 0;
    nbits   = 0;
    wbits   = 0;
    state   = 0;

    while( 1 )
    {
        if( bufsize == 0 )
        {
            if( nblimbs == 0 )
                break;

            nblimbs--;

            bufsize = sizeof( t_uint ) << 3;
        }

        bufsize--;

        ei = (E->p[nblimbs] >> bufsize) & 1;

        /*
         * skip leading 0s
         */
        if( ei == 0 && state == 0 )
            continue;

        if( ei == 0 && state == 1 )
        {
            /*
             * out of window, square X
             */
            sgx_mpi_montmul( X, X, N, mm, &T );
            continue;
        }

        /*
         * add ei to current window
         */
        state = 2;

        nbits++;
        wbits |= ( ei << ( wsize - nbits ) );

        if( nbits == wsize )
        {
            /*
             * X = X^wsize R^-1 mod N
             */
            for( i = 0; i < wsize; i++ )
                sgx_mpi_montmul( X, X, N, mm, &T );

            /*
             * X = X * W[wbits] R^-1 mod N
             */
            sgx_mpi_montmul( X, &W[wbits], N, mm, &T );

            state--;
            nbits = 0;
            wbits = 0;
        }
    }

    /*
     * process the remaining bits
     */
    for( i = 0; i < nbits; i++ )
    {
        sgx_mpi_montmul( X, X, N, mm, &T );

        wbits <<= 1;

        if( ( wbits & ( one << wsize ) ) != 0 )
            sgx_mpi_montmul( X, &W[1], N, mm, &T );
    }

    /*
     * X = A^E * R * R^-1 mod N = A^E mod N
     */
    sgx_mpi_montred( X, N, mm, &T );

    if( neg )
    {
        X->s = -1;
        MPI_CHK( sgx_mpi_add_mpi( X, N, X ) );
    }

cleanup:

    for( i = ( one << ( wsize - 1 ) ); i < ( one << wsize ); i++ )
        sgx_mpi_free( &W[i] );

    sgx_mpi_free( &W[1] ); sgx_mpi_free( &T ); sgx_mpi_free( &Apos );

    if( _RR == NULL || _RR->p == NULL )
        sgx_mpi_free( &RR );

    return( ret );
}

/*
 * Greatest common divisor: G = gcd(A, B)  (HAC 14.54)
 */
int sgx_mpi_gcd( mpi *G, const mpi *A, const mpi *B )
{
    int ret;
    size_t lz, lzt;
    mpi TG, TA, TB;

    sgx_mpi_init( &TG ); sgx_mpi_init( &TA ); sgx_mpi_init( &TB );

    MPI_CHK( sgx_mpi_copy( &TA, A ) );
    MPI_CHK( sgx_mpi_copy( &TB, B ) );

    lz = sgx_mpi_lsb( &TA );
    lzt = sgx_mpi_lsb( &TB );

    if( lzt < lz )
        lz = lzt;

    MPI_CHK( sgx_mpi_shift_r( &TA, lz ) );
    MPI_CHK( sgx_mpi_shift_r( &TB, lz ) );

    TA.s = TB.s = 1;

    while( sgx_mpi_cmp_int( &TA, 0 ) != 0 )
    {
        MPI_CHK( sgx_mpi_shift_r( &TA, sgx_mpi_lsb( &TA ) ) );
        MPI_CHK( sgx_mpi_shift_r( &TB, sgx_mpi_lsb( &TB ) ) );

        if( sgx_mpi_cmp_mpi( &TA, &TB ) >= 0 )
        {
            MPI_CHK( sgx_mpi_sub_abs( &TA, &TA, &TB ) );
            MPI_CHK( sgx_mpi_shift_r( &TA, 1 ) );
        }
        else
        {
            MPI_CHK( sgx_mpi_sub_abs( &TB, &TB, &TA ) );
            MPI_CHK( sgx_mpi_shift_r( &TB, 1 ) );
        }
    }

    MPI_CHK( sgx_mpi_shift_l( &TB, lz ) );
    MPI_CHK( sgx_mpi_copy( G, &TB ) );

cleanup:

    sgx_mpi_free( &TG ); sgx_mpi_free( &TA ); sgx_mpi_free( &TB );

    return( ret );
}

/*
 * Fill X with size bytes of random.
 *
 * Use a temporary bytes representation to make sure the result is the same
 * regardless of the platform endianness (useful when f_rng is actually
 * deterministic, eg for tests).
 */
//int mpi_fill_random( mpi *X, size_t size,
//                     int (*f_rng)(void *, unsigned char *, size_t),
//                     void *p_rng )
int sgx_mpi_fill_random( mpi *X, size_t size,
                     void *p_rng )
{
    int ret;
    unsigned char buf[POLARSSL_MPI_MAX_SIZE];

    if( size > POLARSSL_MPI_MAX_SIZE )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    MPI_CHK( sgx_ctr_drbg_random( p_rng, buf, size ) );
    MPI_CHK( sgx_mpi_read_binary( X, buf, size ) );

cleanup:
    return( ret );
}

/*
 * Modular inverse: X = A^-1 mod N  (HAC 14.61 / 14.64)
 */
int sgx_mpi_inv_mod( mpi *X, const mpi *A, const mpi *N )
{
    int ret;
    mpi G, TA, TU, U1, U2, TB, TV, V1, V2;

    if( sgx_mpi_cmp_int( N, 0 ) <= 0 )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    sgx_mpi_init( &TA ); sgx_mpi_init( &TU ); sgx_mpi_init( &U1 ); sgx_mpi_init( &U2 );
    sgx_mpi_init( &G ); sgx_mpi_init( &TB ); sgx_mpi_init( &TV );
    sgx_mpi_init( &V1 ); sgx_mpi_init( &V2 );

    MPI_CHK( sgx_mpi_gcd( &G, A, N ) );

    if( sgx_mpi_cmp_int( &G, 1 ) != 0 )
    {
        ret = POLARSSL_ERR_MPI_NOT_ACCEPTABLE;
        goto cleanup;
    }

    MPI_CHK( sgx_mpi_mod_mpi( &TA, A, N ) );
    MPI_CHK( sgx_mpi_copy( &TU, &TA ) );
    MPI_CHK( sgx_mpi_copy( &TB, N ) );
    MPI_CHK( sgx_mpi_copy( &TV, N ) );

    MPI_CHK( sgx_mpi_lset( &U1, 1 ) );
    MPI_CHK( sgx_mpi_lset( &U2, 0 ) );
    MPI_CHK( sgx_mpi_lset( &V1, 0 ) );
    MPI_CHK( sgx_mpi_lset( &V2, 1 ) );

    do
    {
        while( ( TU.p[0] & 1 ) == 0 )
        {
            MPI_CHK( sgx_mpi_shift_r( &TU, 1 ) );

            if( ( U1.p[0] & 1 ) != 0 || ( U2.p[0] & 1 ) != 0 )
            {
                MPI_CHK( sgx_mpi_add_mpi( &U1, &U1, &TB ) );
                MPI_CHK( sgx_mpi_sub_mpi( &U2, &U2, &TA ) );
            }

            MPI_CHK( sgx_mpi_shift_r( &U1, 1 ) );
            MPI_CHK( sgx_mpi_shift_r( &U2, 1 ) );
        }

        while( ( TV.p[0] & 1 ) == 0 )
        {
            MPI_CHK( sgx_mpi_shift_r( &TV, 1 ) );

            if( ( V1.p[0] & 1 ) != 0 || ( V2.p[0] & 1 ) != 0 )
            {
                MPI_CHK( sgx_mpi_add_mpi( &V1, &V1, &TB ) );
                MPI_CHK( sgx_mpi_sub_mpi( &V2, &V2, &TA ) );
            }

            MPI_CHK( sgx_mpi_shift_r( &V1, 1 ) );
            MPI_CHK( sgx_mpi_shift_r( &V2, 1 ) );
        }

        if( sgx_mpi_cmp_mpi( &TU, &TV ) >= 0 )
        {
            MPI_CHK( sgx_mpi_sub_mpi( &TU, &TU, &TV ) );
            MPI_CHK( sgx_mpi_sub_mpi( &U1, &U1, &V1 ) );
            MPI_CHK( sgx_mpi_sub_mpi( &U2, &U2, &V2 ) );
        }
        else
        {
            MPI_CHK( sgx_mpi_sub_mpi( &TV, &TV, &TU ) );
            MPI_CHK( sgx_mpi_sub_mpi( &V1, &V1, &U1 ) );
            MPI_CHK( sgx_mpi_sub_mpi( &V2, &V2, &U2 ) );
        }
    }
    while( sgx_mpi_cmp_int( &TU, 0 ) != 0 );

    while( sgx_mpi_cmp_int( &V1, 0 ) < 0 )
        MPI_CHK( sgx_mpi_add_mpi( &V1, &V1, N ) );

    while( sgx_mpi_cmp_mpi( &V1, N ) >= 0 )
        MPI_CHK( sgx_mpi_sub_mpi( &V1, &V1, N ) );

    MPI_CHK( sgx_mpi_copy( X, &V1 ) );

cleanup:

    sgx_mpi_free( &TA ); sgx_mpi_free( &TU ); sgx_mpi_free( &U1 ); sgx_mpi_free( &U2 );
    sgx_mpi_free( &G ); sgx_mpi_free( &TB ); sgx_mpi_free( &TV );
    sgx_mpi_free( &V1 ); sgx_mpi_free( &V2 );

    return( ret );
}

#if defined(POLARSSL_GENPRIME)

static const int small_prime[] =
{
        3,    5,    7,   11,   13,   17,   19,   23,
       29,   31,   37,   41,   43,   47,   53,   59,
       61,   67,   71,   73,   79,   83,   89,   97,
      101,  103,  107,  109,  113,  127,  131,  137,
      139,  149,  151,  157,  163,  167,  173,  179,
      181,  191,  193,  197,  199,  211,  223,  227,
      229,  233,  239,  241,  251,  257,  263,  269,
      271,  277,  281,  283,  293,  307,  311,  313,
      317,  331,  337,  347,  349,  353,  359,  367,
      373,  379,  383,  389,  397,  401,  409,  419,
      421,  431,  433,  439,  443,  449,  457,  461,
      463,  467,  479,  487,  491,  499,  503,  509,
      521,  523,  541,  547,  557,  563,  569,  571,
      577,  587,  593,  599,  601,  607,  613,  617,
      619,  631,  641,  643,  647,  653,  659,  661,
      673,  677,  683,  691,  701,  709,  719,  727,
      733,  739,  743,  751,  757,  761,  769,  773,
      787,  797,  809,  811,  821,  823,  827,  829,
      839,  853,  857,  859,  863,  877,  881,  883,
      887,  907,  911,  919,  929,  937,  941,  947,
      953,  967,  971,  977,  983,  991,  997, -103
};

/*
 * Small divisors test (X must be positive)
 *
 * Return values:
 * 0: no small factor (possible prime, more tests needed)
 * 1: certain prime
 * POLARSSL_ERR_MPI_NOT_ACCEPTABLE: certain non-prime
 * other negative: error
 */
static int sgx_mpi_check_small_factors( const mpi *X )
{
    int ret = 0;
    size_t i;
    t_uint r;

    if( ( X->p[0] & 1 ) == 0 )
        return( POLARSSL_ERR_MPI_NOT_ACCEPTABLE );

    for( i = 0; small_prime[i] > 0; i++ )
    {
        if( sgx_mpi_cmp_int( X, small_prime[i] ) <= 0 )
            return( 1 );

        MPI_CHK( sgx_mpi_mod_int( &r, X, small_prime[i] ) );

        if( r == 0 )
            return( POLARSSL_ERR_MPI_NOT_ACCEPTABLE );
    }

cleanup:
    return( ret );
}

/*
 * Miller-Rabin pseudo-primality test  (HAC 4.24)
 */
//static int mpi_miller_rabin( const mpi *X,
//                             int (*f_rng)(void *, unsigned char *, size_t),
//                             void *p_rng )
static int sgx_mpi_miller_rabin( const mpi *X,
                             void *p_rng )
{
    int ret;
    size_t i, j, n, s;
    mpi W, R, T, A, RR;

    sgx_mpi_init( &W ); sgx_mpi_init( &R ); sgx_mpi_init( &T ); sgx_mpi_init( &A );
    sgx_mpi_init( &RR );

    /*
     * W = |X| - 1
     * R = W >> lsb( W )
     */
    MPI_CHK( sgx_mpi_sub_int( &W, X, 1 ) );
    s = sgx_mpi_lsb( &W );
    MPI_CHK( sgx_mpi_copy( &R, &W ) );
    MPI_CHK( sgx_mpi_shift_r( &R, s ) );

    i = sgx_mpi_msb( X );
    /*
     * HAC, table 4.4
     */
    n = ( ( i >= 1300 ) ?  2 : ( i >=  850 ) ?  3 :
          ( i >=  650 ) ?  4 : ( i >=  350 ) ?  8 :
          ( i >=  250 ) ? 12 : ( i >=  150 ) ? 18 : 27 );

    for( i = 0; i < n; i++ )
    {
        /*
         * pick a random A, 1 < A < |X| - 1
         */
//        MPI_CHK( mpi_fill_random( &A, X->n * ciL, f_rng, p_rng ) );
        MPI_CHK( sgx_mpi_fill_random( &A, X->n * ciL, p_rng ) );

        if( sgx_mpi_cmp_mpi( &A, &W ) >= 0 )
        {
            j = sgx_mpi_msb( &A ) - sgx_mpi_msb( &W );
            MPI_CHK( sgx_mpi_shift_r( &A, j + 1 ) );
        }
        A.p[0] |= 3;

        /*
         * A = A^R mod |X|
         */
        MPI_CHK( sgx_mpi_exp_mod( &A, &A, &R, X, &RR ) );

        if( sgx_mpi_cmp_mpi( &A, &W ) == 0 ||
            sgx_mpi_cmp_int( &A,  1 ) == 0 )
            continue;

        j = 1;
        while( j < s && sgx_mpi_cmp_mpi( &A, &W ) != 0 )
        {
            /*
             * A = A * A mod |X|
             */
            MPI_CHK( sgx_mpi_mul_mpi( &T, &A, &A ) );
            MPI_CHK( sgx_mpi_mod_mpi( &A, &T, X  ) );

            if( sgx_mpi_cmp_int( &A, 1 ) == 0 )
                break;

            j++;
        }

        /*
         * not prime if A != |X| - 1 or A == 1
         */
        if( sgx_mpi_cmp_mpi( &A, &W ) != 0 ||
            sgx_mpi_cmp_int( &A,  1 ) == 0 )
        {
            ret = POLARSSL_ERR_MPI_NOT_ACCEPTABLE;
            break;
        }
    }

cleanup:
    sgx_mpi_free( &W ); sgx_mpi_free( &R ); sgx_mpi_free( &T ); sgx_mpi_free( &A );
    sgx_mpi_free( &RR );

    return( ret );
}

/*
 * Pseudo-primality test: small factors, then Miller-Rabin
 */
//int mpi_is_prime( mpi *X,
//                  int (*f_rng)(void *, unsigned char *, size_t),
//                  void *p_rng )
int sgx_mpi_is_prime( mpi *X,
                  void *p_rng )
{
    int ret;
    const mpi XX = { 1, X->n, X->p }; /* Abs(X) */

    if( sgx_mpi_cmp_int( &XX, 0 ) == 0 ||
        sgx_mpi_cmp_int( &XX, 1 ) == 0 )
        return( POLARSSL_ERR_MPI_NOT_ACCEPTABLE );

    if( sgx_mpi_cmp_int( &XX, 2 ) == 0 )
        return( 0 );

    if( ( ret = sgx_mpi_check_small_factors( &XX ) ) != 0 )
    {
        if( ret == 1 )
            return( 0 );

        return( ret );
    }

//    return( sgx_mpi_miller_rabin( &XX, f_rng, p_rng ) );
    return( sgx_mpi_miller_rabin( &XX, p_rng ) );
}

/*
 * Prime number generation
 */
//int mpi_gen_prime( mpi *X, size_t nbits, int dh_flag,
//                   int (*f_rng)(void *, unsigned char *, size_t),
//                   void *p_rng )
int sgx_mpi_gen_prime( mpi *X, size_t nbits, int dh_flag,
                   void *p_rng )
{
    int ret;
    size_t k, n;
    t_uint r;
    mpi Y;

    if( nbits < 3 || nbits > POLARSSL_MPI_MAX_BITS )
        return( POLARSSL_ERR_MPI_BAD_INPUT_DATA );

    sgx_mpi_init( &Y );

    n = BITS_TO_LIMBS( nbits );

//    MPI_CHK( mpi_fill_random( X, n * ciL, f_rng, p_rng ) );
    MPI_CHK( sgx_mpi_fill_random( X, n * ciL, p_rng ) );

    k = sgx_mpi_msb( X );
    if( k < nbits ) MPI_CHK( sgx_mpi_shift_l( X, nbits - k ) );
    if( k > nbits ) MPI_CHK( sgx_mpi_shift_r( X, k - nbits ) );

    X->p[0] |= 3;

    if( dh_flag == 0 )
    {
//        while( ( ret = mpi_is_prime( X, f_rng, p_rng ) ) != 0 )
        while( ( ret = sgx_mpi_is_prime( X, p_rng ) ) != 0 )
        {
            if( ret != POLARSSL_ERR_MPI_NOT_ACCEPTABLE )
                goto cleanup;

            MPI_CHK( sgx_mpi_add_int( X, X, 2 ) );
        }
    }
    else
    {
        /*
         * An necessary condition for Y and X = 2Y + 1 to be prime
         * is X = 2 mod 3 (which is equivalent to Y = 2 mod 3).
         * Make sure it is satisfied, while keeping X = 3 mod 4
         */
        MPI_CHK( sgx_mpi_mod_int( &r, X, 3 ) );
        if( r == 0 )
            MPI_CHK( sgx_mpi_add_int( X, X, 8 ) );
        else if( r == 1 )
            MPI_CHK( sgx_mpi_add_int( X, X, 4 ) );

        /* Set Y = (X-1) / 2, which is X / 2 because X is odd */
        MPI_CHK( sgx_mpi_copy( &Y, X ) );
        MPI_CHK( sgx_mpi_shift_r( &Y, 1 ) );

        while( 1 )
        {
            /*
             * First, check small factors for X and Y
             * before doing Miller-Rabin on any of them
             */
            if( ( ret = sgx_mpi_check_small_factors(  X         ) ) == 0 &&
                ( ret = sgx_mpi_check_small_factors( &Y         ) ) == 0 &&
                ( ret = sgx_mpi_miller_rabin(  X, p_rng  ) ) == 0 &&
                ( ret = sgx_mpi_miller_rabin( &Y, p_rng  ) ) == 0 )
//                ( ret = mpi_miller_rabin(  X, f_rng, p_rng  ) ) == 0 &&
//                ( ret = mpi_miller_rabin( &Y, f_rng, p_rng  ) ) == 0 )
            {
                break;
            }

            if( ret != POLARSSL_ERR_MPI_NOT_ACCEPTABLE )
                goto cleanup;

            /*
             * Next candidates. We want to preserve Y = (X-1) / 2 and
             * Y = 1 mod 2 and Y = 2 mod 3 (eq X = 3 mod 4 and X = 2 mod 3)
             * so up Y by 6 and X by 12.
             */
            MPI_CHK( sgx_mpi_add_int(  X,  X, 12 ) );
            MPI_CHK( sgx_mpi_add_int( &Y, &Y, 6  ) );
        }
    }

cleanup:

    sgx_mpi_free( &Y );

    return( ret );
}

#if 0
#endif /* POLARSSL_GENPRIME */

#if defined(POLARSSL_SELF_TEST)

#define GCD_PAIR_COUNT  3

static const int gcd_pairs[GCD_PAIR_COUNT][3] =
{
    { 693, 609, 21 },
    { 1764, 868, 28 },
    { 768454923, 542167814, 1 }
};

/*
 * Checkup routine
 */
int mpi_self_test( int verbose )
{
    int ret, i;
    mpi A, E, N, X, Y, U, V;

    mpi_init( &A ); mpi_init( &E ); mpi_init( &N ); mpi_init( &X );
    mpi_init( &Y ); mpi_init( &U ); mpi_init( &V );

    MPI_CHK( mpi_read_string( &A, 16,
        "EFE021C2645FD1DC586E69184AF4A31E" \
        "D5F53E93B5F123FA41680867BA110131" \
        "944FE7952E2517337780CB0DB80E61AA" \
        "E7C8DDC6C5C6AADEB34EB38A2F40D5E6" ) );

    MPI_CHK( mpi_read_string( &E, 16,
        "B2E7EFD37075B9F03FF989C7C5051C20" \
        "34D2A323810251127E7BF8625A4F49A5" \
        "F3E27F4DA8BD59C47D6DAABA4C8127BD" \
        "5B5C25763222FEFCCFC38B832366C29E" ) );

    MPI_CHK( mpi_read_string( &N, 16,
        "0066A198186C18C10B2F5ED9B522752A" \
        "9830B69916E535C8F047518A889A43A5" \
        "94B6BED27A168D31D4A52F88925AA8F5" ) );

    MPI_CHK( mpi_mul_mpi( &X, &A, &N ) );

    MPI_CHK( mpi_read_string( &U, 16,
        "602AB7ECA597A3D6B56FF9829A5E8B85" \
        "9E857EA95A03512E2BAE7391688D264A" \
        "A5663B0341DB9CCFD2C4C5F421FEC814" \
        "8001B72E848A38CAE1C65F78E56ABDEF" \
        "E12D3C039B8A02D6BE593F0BBBDA56F1" \
        "ECF677152EF804370C1A305CAF3B5BF1" \
        "30879B56C61DE584A0F53A2447A51E" ) );

    if( verbose != 0 )
        polarssl_printf( "  MPI test #1 (mul_mpi): " );

    if( mpi_cmp_mpi( &X, &U ) != 0 )
    {
        if( verbose != 0 )
            polarssl_printf( "failed\n" );

        ret = 1;
        goto cleanup;
    }

    if( verbose != 0 )
        polarssl_printf( "passed\n" );

    MPI_CHK( mpi_div_mpi( &X, &Y, &A, &N ) );

    MPI_CHK( mpi_read_string( &U, 16,
        "256567336059E52CAE22925474705F39A94" ) );

    MPI_CHK( mpi_read_string( &V, 16,
        "6613F26162223DF488E9CD48CC132C7A" \
        "0AC93C701B001B092E4E5B9F73BCD27B" \
        "9EE50D0657C77F374E903CDFA4C642" ) );

    if( verbose != 0 )
        polarssl_printf( "  MPI test #2 (div_mpi): " );

    if( mpi_cmp_mpi( &X, &U ) != 0 ||
        mpi_cmp_mpi( &Y, &V ) != 0 )
    {
        if( verbose != 0 )
            polarssl_printf( "failed\n" );

        ret = 1;
        goto cleanup;
    }

    if( verbose != 0 )
        polarssl_printf( "passed\n" );

    MPI_CHK( mpi_exp_mod( &X, &A, &E, &N, NULL ) );

    MPI_CHK( mpi_read_string( &U, 16,
        "36E139AEA55215609D2816998ED020BB" \
        "BD96C37890F65171D948E9BC7CBAA4D9" \
        "325D24D6A3C12710F10A09FA08AB87" ) );

    if( verbose != 0 )
        polarssl_printf( "  MPI test #3 (exp_mod): " );

    if( mpi_cmp_mpi( &X, &U ) != 0 )
    {
        if( verbose != 0 )
            polarssl_printf( "failed\n" );

        ret = 1;
        goto cleanup;
    }

    if( verbose != 0 )
        polarssl_printf( "passed\n" );

    MPI_CHK( mpi_inv_mod( &X, &A, &N ) );

    MPI_CHK( mpi_read_string( &U, 16,
        "003A0AAEDD7E784FC07D8F9EC6E3BFD5" \
        "C3DBA76456363A10869622EAC2DD84EC" \
        "C5B8A74DAC4D09E03B5E0BE779F2DF61" ) );

    if( verbose != 0 )
        polarssl_printf( "  MPI test #4 (inv_mod): " );

    if( mpi_cmp_mpi( &X, &U ) != 0 )
    {
        if( verbose != 0 )
            polarssl_printf( "failed\n" );

        ret = 1;
        goto cleanup;
    }

    if( verbose != 0 )
        polarssl_printf( "passed\n" );

    if( verbose != 0 )
        polarssl_printf( "  MPI test #5 (simple gcd): " );

    for( i = 0; i < GCD_PAIR_COUNT; i++ )
    {
        MPI_CHK( mpi_lset( &X, gcd_pairs[i][0] ) );
        MPI_CHK( mpi_lset( &Y, gcd_pairs[i][1] ) );

        MPI_CHK( mpi_gcd( &A, &X, &Y ) );

        if( mpi_cmp_int( &A, gcd_pairs[i][2] ) != 0 )
        {
            if( verbose != 0 )
                polarssl_printf( "failed at %d\n", i );

            ret = 1;
            goto cleanup;
        }
    }

    if( verbose != 0 )
        polarssl_printf( "passed\n" );

cleanup:

    if( ret != 0 && verbose != 0 )
        polarssl_printf( "Unexpected error, return code = %08X\n", ret );

    mpi_free( &A ); mpi_free( &E ); mpi_free( &N ); mpi_free( &X );
    mpi_free( &Y ); mpi_free( &U ); mpi_free( &V );

    if( verbose != 0 )
        polarssl_printf( "\n" );

    return( ret );
}

#endif /* POLARSSL_SELF_TEST */

#endif

#endif /* POLARSSL_BIGNUM_C */
