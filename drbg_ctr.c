//
// Created by Ghost on 2019/9/17.
//

#include "drbg_ctr.h"

// inputs with n*block_len and returns output with length block_len
static bool BCC(DRBG_CTR *drbg, const uint8_t *input, uint32_t input_len,
                const uint8_t *key, uint32_t key_len, uint8_t *output) {

    bool ret = true;
    // chaining_value = output
    uint32_t offset = 0;
    uint32_t i;
    uint8_t *input_block = malloc(drbg->conf->block_len);
    memset(output, 0x00, drbg->conf->block_len);

    while (offset < input_len) {

        // input_block = chaining_value ^ block
        for (i = 0; i < drbg->conf->block_len; ++i) {
            input_block[i] = output[i] ^ input[offset];

            // at this time, offset has the maximum value input_len - 1, or the input length IS NOT n*(block_len)
            if (offset >= input_len) {
                ret = false;
                goto cleanup;
            }
            offset++;
        }

        // chaining_value = Block_Encrypt(key, input_block)
        drbg->conf->encrypt(input_block, drbg->conf->block_len, key, key_len, output);
    }

    cleanup:
    free(input_block);
    return ret;
}

static bool Block_Cipher_df(DRBG_CTR *drbg, const uint8_t *input, uint32_t input_len, uint8_t *output, uint32_t output_len) {

    bool ret = true;
    // make slen = n*block_len
    uint32_t slen = 4 + 4 + input_len + 1;
    uint8_t *iv_s, *s;
    uint8_t k[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                     0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    uint8_t *temp = malloc(drbg->conf->key_len + drbg->conf->block_len);
    uint32_t offset = 0, counter = 0, remain;
    // key = leftmost(temp, keylen)
    uint8_t *key = temp;
    // x = select(temp, keylen + 1, keylen + block_len)
    uint8_t *x = &temp[drbg->conf->key_len];
    slen += drbg->conf->block_len - slen % drbg->conf->block_len;
    iv_s = malloc(drbg->conf->block_len + slen);
    s = &iv_s[drbg->conf->block_len];

    if (output_len > MAXIMUM_REQUESTED_BYTES) {
        ret = false;
        goto cleanup;
    }

    // L = len(input_string)
    // N = output_len
    // S = L || N || input_string || 0x80 with 0x00 tailed
    memset(iv_s, 0x00, drbg->conf->block_len + slen);
    s[0] = (uint8_t) ((input_len >> 24u) & 0xffu);
    s[1] = (uint8_t) ((input_len >> 16u) & 0xffu);
    s[2] = (uint8_t) ((input_len >> 8u) & 0xffu);
    s[3] = (uint8_t) (input_len & 0xffu);
    s[4] = (uint8_t) ((output_len >> 24u) & 0xffu);
    s[5] = (uint8_t) ((output_len >> 16u) & 0xffu);
    s[6] = (uint8_t) ((output_len >> 8u) & 0xffu);
    s[7] = (uint8_t) (output_len & 0xffu);
    memcpy(&s[8], input, input_len);
    s[8 + input_len] = 0x80;

    while (offset < drbg->conf->key_len + drbg->conf->block_len) {

        // iv = i || 0x00
        iv_s[0] = (uint8_t) ((counter >> 24u) & 0xffu);
        iv_s[1] = (uint8_t) ((counter >> 16u) & 0xffu);
        iv_s[2] = (uint8_t) ((counter >> 8u) & 0xffu);
        iv_s[3] = (uint8_t) (counter & 0xffu);

        // temp = temp || BCC(K, (iv||s))
        if ((ret = BCC(drbg, iv_s, drbg->conf->block_len + slen, k, drbg->conf->key_len, &temp[offset])) != true)
            goto cleanup;

        offset += drbg->conf->block_len;
        counter++;
    }

    for (remain = output_len;; remain -= drbg->conf->block_len) {

        // X = Block_Encrypt(K, X), temp = temp || x till full of return length long bytes
        if ((ret = drbg->conf->encrypt(
                &temp[drbg->conf->key_len], drbg->conf->block_len,
                key, drbg->conf->key_len, x)) != true)
            goto cleanup;

        // use the smaller one size, hash outputs up to hash_size length
        if (remain > drbg->conf->block_len) memcpy(&output[output_len - remain], x, drbg->conf->block_len);
        else {
            // output remains less than out_len, which reaches final block
            memcpy(&output[output_len - remain], x, remain);
            break;
        }
    }

    cleanup:
    free(temp);
    free(iv_s);
    return ret;
}

// modify from openssl/crypto/rand/drbg_hash.c
static bool add(uint8_t *dst, uint32_t dst_len,
                const uint8_t *in, uint32_t inlen) {
    uint32_t i;
    uint32_t result;
    const uint8_t *add;
    uint8_t carry = 0, *d;

    if (!(dst_len >= 1 && inlen >= 1 && inlen <= dst_len)) return false;

    d = &dst[dst_len - 1];
    add = &in[inlen - 1];

    for (i = inlen; i > 0; i--, d--, add--) {
        result = *d + *add + carry;
        carry = (uint8_t) (result >> 8u);
        *d = (uint8_t) (result & 0xffu);
    }

    if (carry != 0) {
        /* Add the carry to the top of the dst if inlen is not the same size */
        for (i = dst_len - inlen; i > 0; --i, d--) {
            *d += 1;     /* Carry can only be 1 */
            if (*d != 0) /* exit if carry doesnt propagate to the next byte */
                break;
        }
    }
    return true;
}

static bool DRBG_CTR_Update(DRBG_CTR *drbg, const uint8_t *input, uint32_t input_len) {

    bool ret = true;
    uint32_t temp_len = drbg->conf->block_len +
                        (drbg->conf->key_len / drbg->conf->block_len) * drbg->conf->block_len +
                        (drbg->conf->key_len % drbg->conf->block_len == 0 ? 0 : drbg->conf->block_len);
    uint8_t *temp = malloc(temp_len);
    uint32_t offset = 0;
    uint8_t one = 1;

    if (input_len < drbg->conf->block_len + drbg->conf->key_len) {
        ret = false;
        goto cleanup;
    }

    while (offset < drbg->conf->block_len + drbg->conf->key_len) {
        if (drbg->conf->ctr_len < drbg->conf->block_len) {
            ret = add(&drbg->V[drbg->conf->block_len - drbg->conf->ctr_len], drbg->conf->ctr_len, &one, 1);
        } else {
            ret = add(drbg->V, drbg->conf->block_len, &one, 1);
        }
        if (ret != true) goto cleanup;

        // temp = temp || Block_Encrypt(key, v)
        ret = drbg->conf->encrypt(drbg->V, drbg->conf->block_len, drbg->key, drbg->conf->key_len, &temp[offset]);
        offset += drbg->conf->block_len;
        if (ret != true) goto cleanup;
    }

    // temp ^= provided_data
    for (uint32_t i = 0; i < drbg->conf->block_len + drbg->conf->key_len; ++i) {
        temp[i] ^= input[i];
    }

    memcpy(drbg->key, temp, drbg->conf->key_len);
    memcpy(drbg->V, &temp[drbg->conf->key_len], drbg->conf->block_len);

    cleanup:
    free(temp);
    return ret;
}

bool DRBG_CTR_new(DRBG_CTR *drbg, DRBG_CTR_CONF *conf, uint8_t useDerivationFunction) {

    uint8_t *v;
    uint8_t *key;
    // validate config
    if (conf == NULL || conf->encrypt == NULL ||
        conf->block_len == 0 ||
        conf->key_len == 0 ||
        conf->ctr_len < 4 || conf->ctr_len > conf->block_len ||
        conf->reseed_interval == 0) {
        return false;
    }

    conf->useDerivationFunction = useDerivationFunction;
    drbg->conf = conf;

    /* initialize internal state */
    v = malloc(conf->block_len);
    memset(v, 0, conf->block_len);
    drbg->V = v;
    key = malloc(conf->key_len);
    memset(key, 0, conf->key_len);
    drbg->key = key;
    drbg->reseed_counter = 0;
    drbg->prediction_resistance_flag = 0;
    return true;
}

bool DRBG_CTR_instantiate(DRBG_CTR *drbg,
                          const uint8_t *entropy, uint32_t entropy_length,
                          const uint8_t *nonce, uint32_t nonce_length,
                          const uint8_t *pstr, uint32_t pstr_length) {

    bool ret = true;
    uint32_t seed_len = drbg->conf->key_len + drbg->conf->block_len;
    uint8_t *seed_mat = malloc(seed_len);
    uint8_t current;
    uint32_t i;

    for (i = 0; i < seed_len; ++i) {
        if (i < pstr_length) current = pstr[i];
        else if (nonce != NULL && pstr_length <= i && i < pstr_length + nonce_length) current = nonce[i];
        else current = 0x00;
        if (i < entropy_length) seed_mat[i] = entropy[i] ^ current;
        else seed_mat[i] = current;
    }

    if (drbg->conf->useDerivationFunction) {

        // seed_material = Block_cipher_df(seed_material, seedlen)
        ret = Block_Cipher_df(drbg, seed_mat, seed_len, seed_mat, seed_len);
        if (ret != true) goto cleanup;
    }

    memset(drbg->key, 0x00, drbg->conf->key_len);
    memset(drbg->V, 0x00, drbg->conf->block_len);
    ret = DRBG_CTR_Update(drbg, seed_mat, seed_len);
    drbg->reseed_counter = 1;

    cleanup:
    free(seed_mat);
    return ret;
}

bool DRBG_CTR_reseed(DRBG_CTR *drbg,
                     const uint8_t *entropy, uint32_t entropy_length,
                     const uint8_t *add_input, uint32_t add_length) {

    bool ret = true;
    uint32_t seed_len = drbg->conf->key_len + drbg->conf->block_len;
    uint8_t *seed_mat = malloc(seed_len);
    uint8_t current;
    uint32_t i;

    for (i = 0; i < seed_len; ++i) {
        if (i < add_length) current = add_input[i];
        else current = 0x00;
        if (i < entropy_length) seed_mat[i] = entropy[i] ^ current;
        else seed_mat[i] = current;
    }

    if (drbg->conf->useDerivationFunction) {

        // seed_material = Block_cipher_df(seed_material, seedlen)
        ret = Block_Cipher_df(drbg, seed_mat, seed_len, seed_mat, seed_len);
        if (ret != true) goto cleanup;
    }

    ret = DRBG_CTR_Update(drbg, seed_mat, seed_len);
    drbg->reseed_counter = 1;

    cleanup:
    free(seed_mat);
    return ret;
}

bool DRBG_CTR_generate(DRBG_CTR *drbg,
                       const uint8_t *add_input, uint32_t add_length,
                       uint8_t *output, uint32_t return_length) {

    bool ret;
    uint32_t seed_len = drbg->conf->key_len + drbg->conf->block_len;
    uint8_t *add_temp = malloc(seed_len);
    uint8_t *temp = malloc(drbg->conf->block_len);
    uint8_t one = 1;
    uint32_t remain;

    memset(add_temp, 0x00, seed_len);
    if (add_input != NULL) {
        if (drbg->conf->useDerivationFunction) {
            ret = Block_Cipher_df(drbg, add_input, add_length, add_temp, seed_len);
            if (ret != true) goto cleanup;
        } else {
            memcpy(add_temp, add_input, add_length);
        }

        DRBG_CTR_Update(drbg, add_temp, seed_len);
    }

    for (remain = return_length;; remain -= drbg->conf->block_len) {

        if (drbg->conf->ctr_len < drbg->conf->block_len) {
            ret = add(&drbg->V[drbg->conf->block_len - drbg->conf->ctr_len], drbg->conf->ctr_len, &one, 1);
            if (ret != true) goto cleanup;
        } else {
            ret = add(drbg->V, drbg->conf->block_len, &one, 1);
            if (ret != true) goto cleanup;
        }

        // temp = temp || Block_Encrypt(K, V) till full of return length long bytes
        if ((ret = drbg->conf->encrypt(
                drbg->V, drbg->conf->block_len,
                drbg->key, drbg->conf->key_len, temp)) != true)
            goto cleanup;

        // use the smaller one size, hash outputs up to hash_size length
        if (remain > drbg->conf->block_len)
            memcpy(&output[return_length - remain], temp, drbg->conf->block_len);
        else {
            // output remains less than out_len, which reaches final block
            memcpy(&output[return_length - remain], temp, remain);
            break;
        }
    }

    ret = DRBG_CTR_Update(drbg, add_temp, seed_len);
    drbg->reseed_counter++;

    cleanup:
    free(add_temp);
    free(temp);
    return ret;
}

bool DRBG_CTR_uninstantiate(DRBG_CTR *drbg) {

    // clear V and Key state
    memset(drbg->V, 0, drbg->conf->block_len);
    free(drbg->V);
    drbg->V = NULL;
    memset(drbg->key, 0, drbg->conf->key_len);
    free(drbg->key);
    drbg->key = NULL;
    drbg->reseed_counter = 0;

    return true;
}