// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <openenclave/bits/defs.h>
#include <openenclave/bits/safecrt.h>
#include <openenclave/internal/raise.h>
#include <openenclave/internal/report.h>
#include <openenclave/internal/sgxtypes.h>
#include <openenclave/internal/utils.h>

#include <openenclave/internal/enclavelibc.h>
#include "../common/common.h"
#include <openenclave/internal/print.h>
#include <stdio.h>

// Using mbedtls to create an extended X.509 certificate
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/certs.h>
#include <mbedtls/x509.h>
#include <mbedtls/ssl.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#define MAX_CERT_SIZE 8*1024
#define UNREFERENCED(x) (void(x)) // Prevent unused warning

//#include <mbedtls/net_sockets.h>
static unsigned char _cert_buf[MAX_CERT_SIZE] = {0, };

const size_t SHA256_DIGEST_SIZE = 32;

void _gen_sha256(char* data, size_t data_size, OE_SHA256* sha256)
{
    oe_sha256_context_t ctx = {0};
    oe_sha256_init(&ctx);
    oe_sha256_update(&ctx, data, data_size);
    oe_sha256_final(&ctx, sha256);
}

void sha256_rsa_pubkey(unsigned char hash[SHA256_DIGEST_SIZE],
                       const mbedtls_pk_context* pk) 
{
    static const int pk_der_size_max = 512;
    uint8_t pk_der[pk_der_size_max];
    oe_memset(pk_der, 0, pk_der_size_max);


    /* From the mbedtls documentation: Write a public key to a
       SubjectPublicKeyInfo DER structure Note: data is written at the
       end of the buffer! Use the return value to determine where you
       should start using the buffer. */
    int pk_der_size_byte = mbedtls_pk_write_pubkey_der((mbedtls_pk_context*) pk,
                                                       pk_der, pk_der_size_max);
    // Can only handle 2048 bit RSA keys for now. Other key sizes will
    // have a different pk_der_offset.
    //assert(pk_der_size_byte == 294);

    /* Move the data to the beginning of the buffer, to avoid pointer
       arithmetic from this point forward. */
 //Todo   memmove(pk_der, pk_der + pk_der_size_max - pk_der_size_byte, pk_der_size_byte);

    /* 24 since we skip the DER structure header (i.e, PKCS#1 header). */
    static const size_t pk_der_offset = 24;

    oe_memset(hash, 0, SHA256_DIGEST_SIZE);
    mbedtls_sha256_ret(pk_der + pk_der_offset,
                        (size_t)pk_der_size_byte - pk_der_offset,
                        hash, 0 /* is224 */);
}

oe_result_t generate_x509_cert( oe_cert_format_t cert_format,
                                uint8_t*public_key,
                                size_t public_key_size,
                                uint8_t*private_key,
                                size_t private_key_size,
                                uint8_t** output_cert,
                                size_t* output_cert_size,
                                char* user_data,
                                size_t user_data_size)
{
    oe_result_t result = OE_FAILURE;
    uint8_t* remote_report_buf = NULL;
    size_t remote_report_buf_size = OE_MAX_REPORT_SIZE;
    mbedtls_mpi serial;
    OE_SHA256 sha256 = {0};
    mbedtls_x509write_cert x509cert = { 0 };
    mbedtls_pk_context subject_key; // public key
    mbedtls_pk_context issuer_key;  // private key
    int ret = 0;
    size_t bytes_written = 0;
    uint8_t *host_cert_buf = NULL;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_pk_init(&subject_key);
    mbedtls_pk_init(&issuer_key);

    mbedtls_mpi_init(&serial);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // create pk_context for both public and private keys
    ret = mbedtls_pk_parse_public_key(&subject_key,
                                     (const unsigned char *)public_key,
                                      public_key_size);
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    ret = mbedtls_pk_parse_key(&issuer_key,
                               (const unsigned char *)private_key,
                               private_key_size, NULL, 0);
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    //
    // get attestation data
    //

    // we can optionally add user data into the report via 
    // deposting the hash of user data in the sgx report_data field
    _gen_sha256(user_data, user_data_size, &sha256);

    // Generate a remote report for the public key so that the enclave that
    // receives the key can attest this enclave.
    remote_report_buf = (uint8_t*)oe_malloc(OE_MAX_REPORT_SIZE);
    if (remote_report_buf == NULL)
    {
        goto done;
    }

    result = oe_get_report(
        OE_REPORT_FLAGS_REMOTE_ATTESTATION,
        sha256.buf,         // Store sha256 in report_data field
        sizeof(sha256.buf),
        NULL,               // opt_params must be null
        0,
        remote_report_buf,
        &remote_report_buf_size);
    OE_CHECK_MSG(result, "oe_get_report failed with %s\n", oe_result_str(result));

    // generate certificate 
    mbedtls_x509write_crt_init(&x509cert);
    mbedtls_x509write_crt_set_md_alg(&x509cert, MBEDTLS_MD_SHA256);

    // same key for both issuer and subject in the certificate
    mbedtls_x509write_crt_set_subject_key(&x509cert, &subject_key);
    mbedtls_x509write_crt_set_issuer_key(&x509cert, &issuer_key);

    // Set the subject name for a Certificate Subject names should contain a comma-separated list of OID types and values:
    // e.g. "C=UK,O=ARM,CN=mbed TLS Server 1"

    ret = mbedtls_x509write_crt_set_subject_name(&x509cert, "CN=Open Encalve SDK,O=OESDK TLS,C=UK");
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    //assert(ret == 0);
    ret = mbedtls_x509write_crt_set_issuer_name(&x509cert, "CN=Open Encalve SDK,O=OESDK TLS,C=UK");
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    ret = mbedtls_mpi_read_string(&serial, 10, "1");
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    ret = mbedtls_x509write_crt_set_serial(&x509cert, &serial);
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    // Set the validity period for a Certificate Timestamps
    // get time from the host for not_before 
    // and plus 10 years for the not_after
    ret = mbedtls_x509write_crt_set_validity(&x509cert,
                                             "20180101000000", // not_before
                                             "20501231235959"); // not_after
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    // Set the basicConstraints extension for a CRT
    ret = mbedtls_x509write_crt_set_basic_constraints(&x509cert, 
                                                     0, // is_ca
                                                     -1);
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);

    // Set the subjectKeyIdentifier extension for a CRT Requires that
    // mbedtls_x509write_crt_set_subject_key() has been called before
    ret = mbedtls_x509write_crt_set_subject_key_identifier(&x509cert);
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret); 

    // Set the authorityKeyIdentifier extension for a CRT Requires that
    // mbedtls_x509write_crt_set_issuer_key() has been called before.
    ret = mbedtls_x509write_crt_set_authority_key_identifier(&x509cert);
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);        

    //    1.2.840.113556.1000.1 (ISO assigned OIDs, ISO member body, USA, Microsoft)
    // Need to get a registered OID from the following site
    // https://www.alvestrand.no/objectid/1.2.840.113556.html

    // // 1.2.840.113741.1337.3
    // unsigned char oid_ias_sign_ca_cert[] = {0x2A, 0x86, 0x48, 0x86, 0xF8, 0x4D, 0x8A, 0x39, 0x03};
#if 1
    unsigned char oid_oe_report[] = {0x2A, 0x86, 0x48, 0x86, 0xF8, 0x4D, 0x8A, 0x39, 0x01};
    ret = mbedtls_x509write_crt_set_extension(&x509cert,
                                              (char*) oid_oe_report,
                                              sizeof(oid_oe_report),
                                              0 /* criticial */,
                                              (const uint8_t*) remote_report_buf,
                                              remote_report_buf_size);
    if (ret)
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);
#endif

    // Write a built up certificate to a X509 DER structure Note: data
    // is written at the end of the buffer! Use the return value to
    // determine where you should start using the buffer.
    if (OE_CERT_FORMAT_DER == cert_format) {
        bytes_written = (size_t)mbedtls_x509write_crt_der(&x509cert, _cert_buf, MAX_CERT_SIZE,
                                                  mbedtls_ctr_drbg_random, &ctr_drbg);
        OE_TRACE_INFO("bytes_written = 0x%x", bytes_written);
        if (bytes_written <= 0)
            OE_RAISE_MSG(OE_FAILURE, "bytes_written = 0x%x ", bytes_written);

        // allocate memory for cert output buffer
        host_cert_buf = (uint8_t*)oe_malloc(bytes_written);
        if (host_cert_buf == NULL)
                goto done;
        // copy to host buffer 
        oe_memcpy((void*)host_cert_buf, (const void*)(_cert_buf + sizeof(_cert_buf) - bytes_written), bytes_written);
        //*der_cert_len = bytes_written;

    }
    // else if (OE_CERT_FORMAT_PEM == cert_format)
    // {
    //     ret = mbedtls_x509write_crt_pem(&x509cert, cert_buf, MAX_CERT_SIZE,
    //                                     mbedtls_ctr_drbg_random, &ctr_drbg);
    //     if (ret < 0)
    //         OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);
    //     OE_TRACE_INFO("cert_buf =\n[%s]\n", cert_buf);
    // } 
    else {
        OE_RAISE_MSG(OE_FAILURE, "ret = 0x%x ", ret);
    } 

    *output_cert_size = (size_t)bytes_written;
    *output_cert = host_cert_buf;

done:
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&x509cert);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_pk_free(&issuer_key);
    mbedtls_pk_free(&subject_key);

    if (remote_report_buf)
        oe_free(remote_report_buf);

    if (ret)
    {
        result = OE_FAILURE;
    }
    return result;
}

// input: input_data and input_data_len
// output: key, key_size
oe_result_t generate_key_pair(  uint8_t** public_key,
                                size_t *public_key_size,
                                uint8_t** private_key,
                                size_t *private_key_size)
{
    oe_result_t result = OE_FAILURE;
    oe_asymmetric_key_params_t params = {0};
    char user_data[] = "optional user data!";   
    size_t user_data_size = sizeof(user_data) - 1;

    OE_TRACE_INFO("Generate key pair");

    params.type = OE_ASYMMETRIC_KEY_EC_SECP256P1;  // MBEDTLS_ECP_DP_SECP256R1
    params.format = OE_ASYMMETRIC_KEY_PEM;
    params.user_data = user_data;
    params.user_data_size = user_data_size;
    result = oe_get_public_key_by_policy(
            OE_SEAL_POLICY_UNIQUE,
            &params,
            public_key,
            public_key_size,
            NULL,
            NULL);
    OE_CHECK(result);

    result = oe_get_private_key_by_policy(
            OE_SEAL_POLICY_UNIQUE,
            &params,
            private_key,
            private_key_size,
            NULL,
            NULL);
    OE_CHECK(result);

done:
    return result;
}

// void free_key_pair(uint8_t* public_key,
//                           size_t public_key_size,
//                           uint8_t* private_key,
//                           size_t private_key_size)
// {
//     OE_TRACE_INFO("Freeing public and private key pair");
//     oe_free_key(public_key, public_key_size, NULL, 0);
//     oe_free_key(private_key, private_key_size, NULL, 0);
// }

oe_result_t oe_gen_x509cert_for_TLS(oe_cert_format_t cert_format,
                                    uint8_t** output_cert,
                                    size_t* output_cert_size,
                                    uint8_t** private_key,
                                    size_t* private_key_size)
{
    oe_result_t result = OE_FAILURE;
    uint8_t* public_key = NULL;
    size_t public_key_size = 0;
    // uint8_t* private_key = NULL;
    // size_t private_key_size = 0;
    char user_data[] = "optional user data!";   
    size_t user_data_size = sizeof(user_data) - 1;
    uint8_t* cert_buf = NULL;
    size_t cert_size = 0;

    OE_TRACE_INFO("Calling oe_gen_x509cert_for_TLS");

    // generate public/private key pair
    OE_CHECK(generate_key_pair( &public_key,
                                &public_key_size,
                                private_key,
                                private_key_size));

    OE_TRACE_INFO("public key = \n[%s]\n", public_key);
    OE_TRACE_INFO("private key =\n[%s]\n", private_key);

    // generate cert
    OE_CHECK(generate_x509_cert(cert_format,
                                public_key,
                                public_key_size,
                                *private_key,
                                *private_key_size,
                                output_cert,
                                output_cert_size,
                                user_data, // optional data
                                user_data_size));
    OE_TRACE_INFO("generate_x509_cert succeeded. cert_buf = 0x%p cert_size = %d", cert_buf, cert_size);

    //free_key_pair(public_key, public_key_size, private_key, private_key_size);
    // only free public key
    oe_free_key(public_key, public_key_size, NULL, 0);
    OE_TRACE_INFO("Freeing public key succeeded");

    result = OE_OK;
done:

    return result;
}

void oe_free_x509cert_for_TLS(
    uint8_t* cert,
    size_t cert_size)
{
    OE_TRACE_INFO("Calling oe_free_x509cert_for_TLS cert=0x%p cert_size=0x%x", cert, cert_size);
    fflush(stdout);
    oe_host_free(cert);
}
