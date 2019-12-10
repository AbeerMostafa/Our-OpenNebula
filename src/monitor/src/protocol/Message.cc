/* -------------------------------------------------------------------------- */
/* Copyright 2002-2019, OpenNebula Project, OpenNebula Systems                */
/*                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may    */
/* not use this file except in compliance with the License. You may obtain    */
/* a copy of the License at                                                   */
/*                                                                            */
/* http://www.apache.org/licenses/LICENSE-2.0                                 */
/*                                                                            */
/* Unless required by applicable law or agreed to in writing, software        */
/* distributed under the License is distributed on an "AS IS" BASIS,          */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
/* See the License for the specific language governing permissions and        */
/* limitations under the License.                                             */
/* -------------------------------------------------------------------------- */

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>

#include <zlib.h>

#include "Message.h"
#include "MonitorDriverMessages.h"
#include "OpenNebulaMessages.h"

template<>
const EString<MonitorDriverMessages> Message<MonitorDriverMessages>::_type_str({
    {"UNDEFINED", MonitorDriverMessages::UNDEFINED},
    {"INIT", MonitorDriverMessages::INIT},
    {"FINALIZE", MonitorDriverMessages::FINALIZE},
    {"MONITOR_VM", MonitorDriverMessages::MONITOR_VM},
    {"MONITOR_HOST", MonitorDriverMessages::MONITOR_HOST},
    {"SYSTEM_HOST", MonitorDriverMessages::SYSTEM_HOST},
    {"STATE_VM", MonitorDriverMessages::STATE_VM}
});

template<>
const EString<OpenNebulaMessages> Message<OpenNebulaMessages>::_type_str({
    {"UNDEFINED", OpenNebulaMessages::UNDEFINED},
    {"INIT", OpenNebulaMessages::INIT},
    {"FINALIZE", OpenNebulaMessages::FINALIZE},
    {"UPDATE_HOST", OpenNebulaMessages::UPDATE_HOST},
    {"DEL_HOST", OpenNebulaMessages::DEL_HOST},
});

/* ************************************************************************** */
/* ************************************************************************** */
/* Message helper functions                                                   */
/* ************************************************************************** */
/* ************************************************************************** */

void base64_decode(const std::string& in, std::string& out)
{
    BIO * bio_64  = BIO_new(BIO_f_base64());

    BIO * bio_mem_in  = BIO_new(BIO_s_mem());
    BIO * bio_mem_out = BIO_new(BIO_s_mem());

    bio_64 = BIO_push(bio_64, bio_mem_in);

    BIO_set_flags(bio_64, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(bio_mem_in, in.c_str(), in.length());

    char inbuf[512];
    int  inlen;

    while ((inlen = BIO_read(bio_64, inbuf, 512)) > 0)
    {
        BIO_write(bio_mem_out, inbuf, inlen);
    }

    char * decoded_c;

    long int size = BIO_get_mem_data(bio_mem_out, &decoded_c);

    out.assign(decoded_c, size);

    BIO_free_all(bio_64);
    BIO_free_all(bio_mem_out);
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

int base64_encode(const std::string& in, std::string &out)
{
    BIO * bio_64  = BIO_new(BIO_f_base64());
    BIO * bio_mem = BIO_new(BIO_s_mem());

    BIO_push(bio_64, bio_mem);

    BIO_set_flags(bio_64, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(bio_64, in.c_str(), in.length());

    if (BIO_flush(bio_64) != 1)
    {
        return -1;
    }

    char * encoded_c;

    long int size = BIO_get_mem_data(bio_mem, &encoded_c);

    out.assign(encoded_c, size);

    BIO_free_all(bio_64);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

/**
 *  Buffer length for zlib inflate/deflate
 */
#define ZBUFFER 16384

int zlib_decompress(const std::string& in, std::string& out)
{
    if ( in.empty() )
    {
        return -1;
    }

    z_stream zs;

    zs.zalloc = Z_NULL;
    zs.zfree  = Z_NULL;
    zs.opaque = Z_NULL;

    zs.avail_in = 0;
    zs.next_in  = Z_NULL;

    if ( inflateInit(&zs) != Z_OK)
    {
        return -1;
    }

    zs.avail_in = in.size();
    zs.next_in  = (unsigned char *) const_cast<char *>(in.c_str());

    if ( zs.avail_in <= 2 ) //At least 2 byte header
    {
        inflateEnd(&zs);

        return -1;
    }

    unsigned char zbuf[ZBUFFER];
    int rc;

    do
    {
        zs.avail_out = ZBUFFER;
        zs.next_out  = zbuf;

        rc = inflate(&zs, Z_FINISH);

        if ( rc != Z_STREAM_END && rc != Z_OK && rc != Z_BUF_ERROR )
        {
            inflateEnd(&zs);

            return -1;
        }

        out.append((const char *) zbuf, (size_t) (ZBUFFER - zs.avail_out));
    } while (rc != Z_STREAM_END);

    inflateEnd(&zs);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

int zlib_compress(const std::string& in, std::string& out)
{
    if ( in.empty() )
    {
        return -1;
    }

    z_stream zs;

    zs.zalloc = Z_NULL;
    zs.zfree  = Z_NULL;
    zs.opaque = Z_NULL;

    if ( deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK )
    {
        return -1;
    }

    zs.avail_in = in.size();
    zs.next_in  = (unsigned char *) const_cast<char *>(in.c_str());

    unsigned char zbuf[ZBUFFER];

    do
    {
        zs.avail_out = ZBUFFER;
        zs.next_out  = zbuf;

        if ( deflate(&zs, Z_FINISH) == Z_STREAM_ERROR )
        {
            deflateEnd(&zs);
            return -1;
        }

        out.append((const char *) zbuf, (size_t) (ZBUFFER - zs.avail_out));
    } while (zs.avail_out == 0);

    deflateEnd(&zs);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

//TODO read from configuration path
int rsa_public_encrypt(const std::string& in, std::string& out)
{
    static RSA * rsa = nullptr;

    if ( rsa == nullptr) //initialize RSA structure
    {
        std::string pubk_path = getenv("HOME");
        pubk_path += "/.ssh/id_rsa.pub.pem";

        FILE * fp = fopen(pubk_path.c_str(), "r");

        if ( fp == nullptr )
        {
            return -1;
        }

        rsa = PEM_read_RSAPublicKey(fp, &rsa, nullptr, nullptr);

        if ( rsa == nullptr )
        {
            return -1;
        }

        fclose(fp);
    }

    char * out_c = (char *) malloc(sizeof(char) * RSA_size(rsa));

    int rc = RSA_public_encrypt(in.length(), (const unsigned char *) in.c_str(),
            (unsigned char *) out_c, rsa, RSA_PKCS1_PADDING);

    if ( rc != -1 )
    {
        out.assign(out_c, rc);
        rc = 0;
    }

    free(out_c);

    return rc;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

int rsa_private_decrypt(const std::string& in, std::string& out)
{
    static RSA * rsa = nullptr;

    if ( rsa == nullptr) //initialize RSA structure
    {
        std::string pubk_path = getenv("HOME");
        pubk_path += "/.ssh/id_rsa";

        FILE * fp = fopen(pubk_path.c_str(), "r");

        if ( fp == nullptr )
        {
            return -1;
        }

        rsa = PEM_read_RSAPrivateKey(fp, &rsa, nullptr, nullptr);

        if ( rsa == nullptr )
        {
            return -1;
        }

        fclose(fp);
    }

    char * out_c = (char *) malloc(sizeof(char) * RSA_size(rsa));

    int rc = RSA_private_decrypt(in.length(), (const unsigned char *)in.c_str(),
            (unsigned char *) out_c, rsa, RSA_PKCS1_PADDING);

    if ( rc != -1 )
    {
        out.assign(out_c, rc);
        rc = 0;
    }

    free(out_c);

    return rc;
}
