#include "local_playback.h"

#include <ctype.h>
#include <string.h>

static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool ext_equals(const char *path, const char *ext)
{
    size_t plen = strlen(path), elen = strlen(ext);
    if (plen < elen) return false;
    const char *p = path + plen - elen;
    for (size_t i = 0; i < elen; ++i) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)ext[i])) return false;
    }
    return true;
}

static dev_status_t read_exact(local_playback_t *p, void *buf, size_t len)
{
    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0U;
    while (done < len) {
        size_t got = 0U;
        dev_status_t st = p->fs.read(p->fs.ctx, p->file, dst + done, len - done, &got);
        if (st != DEV_OK) return st;
        if (got == 0U) return DEV_EIO;
        done += got;
    }
    return DEV_OK;
}

static dev_status_t parse_wav(local_playback_t *p)
{
    uint8_t hdr[12];
    dev_status_t st = p->fs.seek(p->fs.ctx, p->file, 0U);
    if (st != DEV_OK) return st;
    st = read_exact(p, hdr, sizeof(hdr));
    if (st != DEV_OK) return st;
    if ((memcmp(hdr, "RIFF", 4U) != 0) || (memcmp(&hdr[8], "WAVE", 4U) != 0)) return DEV_ENOTSUP;

    bool have_fmt = false;
    bool have_data = false;
    uint32_t offset = 12U;
    for (uint16_t chunk = 0U; chunk < 64U; ++chunk) {
        if (offset > UINT32_MAX - 8U) return DEV_EOVERFLOW;
        uint8_t ch[8];
        st = p->fs.seek(p->fs.ctx, p->file, offset);
        if (st != DEV_OK) return st;
        st = read_exact(p, ch, sizeof(ch));
        if (st != DEV_OK) return st;
        uint32_t size = rd32le(&ch[4]);
        uint32_t payload = offset + 8U;

        if (memcmp(ch, "fmt ", 4U) == 0) {
            uint8_t fmt[16];
            if (size < sizeof(fmt)) return DEV_ENOTSUP;
            st = p->fs.seek(p->fs.ctx, p->file, payload);
            if (st != DEV_OK) return st;
            st = read_exact(p, fmt, sizeof(fmt));
            if (st != DEV_OK) return st;
            uint16_t format_tag = rd16le(&fmt[0]);
            p->format.channels = rd16le(&fmt[2]);
            p->format.sample_rate_hz = rd32le(&fmt[4]);
            p->format.bits_per_sample = rd16le(&fmt[14]);
            if ((format_tag != 1U) || (p->format.bits_per_sample != 16U) ||
                (p->format.channels == 0U) || (p->format.channels > 2U) ||
                (p->format.sample_rate_hz < 8000U) ||
                (p->format.sample_rate_hz > 96000U)) return DEV_ENOTSUP;
            have_fmt = true;
        } else if (memcmp(ch, "data", 4U) == 0) {
            p->data_offset = payload;
            p->data_bytes_remaining = size;
            have_data = true;
            if (have_fmt) break;
        }

        uint32_t padding = size & 1U;
        if (size > UINT32_MAX - payload ||
            payload + size > UINT32_MAX - padding) return DEV_EOVERFLOW;
        offset = payload + size + padding;
    }
    if (!have_fmt || !have_data) return DEV_EIO;
    return p->fs.seek(p->fs.ctx, p->file, p->data_offset);
}

static dev_status_t prepare_mp3(local_playback_t *p)
{
    uint8_t hdr[10];
    dev_status_t st = p->fs.seek(p->fs.ctx, p->file, 0U);
    if (st != DEV_OK) return st;

    size_t got = 0U;
    st = p->fs.read(p->fs.ctx, p->file, hdr, sizeof(hdr), &got);
    if (st != DEV_OK) return st;

    uint32_t audio_offset = 0U;
    if (got == sizeof(hdr) && memcmp(hdr, "ID3", 3U) == 0) {
        /* ID3v2 uses a 28-bit synchsafe payload length in bytes 6..9. The
         * encoded size excludes the 10-byte header and, for v2.4, an optional
         * 10-byte footer. Skip metadata/embedded artwork in one seek rather
         * than making minimp3 scan through it in 2 KiB chunks. */
        if (((hdr[6] | hdr[7] | hdr[8] | hdr[9]) & 0x80U) != 0U) return DEV_EIO;
        uint32_t tag_bytes = ((uint32_t)hdr[6] << 21) |
                             ((uint32_t)hdr[7] << 14) |
                             ((uint32_t)hdr[8] << 7) |
                             (uint32_t)hdr[9];
        audio_offset = 10U + tag_bytes;
        if (hdr[3] == 4U && (hdr[5] & 0x10U) != 0U) {
            if (audio_offset > UINT32_MAX - 10U) return DEV_EOVERFLOW;
            audio_offset += 10U;
        }
    }

    p->input_valid = 0U;
    return p->fs.seek(p->fs.ctx, p->file, audio_offset);
}
dev_status_t local_playback_init(local_playback_t *player, const local_fs_t *fs,
                                 const local_pcm_sink_t *sink,
                                 const local_mp3_decoder_t *mp3_decoder)
{
    if ((player == NULL) || (fs == NULL) || (sink == NULL) ||
        (fs->open == NULL) || (fs->read == NULL) || (fs->seek == NULL) ||
        (fs->close == NULL) || (sink->write == NULL)) return DEV_EINVAL;
    memset(player, 0, sizeof(*player));
    player->fs = *fs;
    player->sink = *sink;
    if (mp3_decoder != NULL) player->mp3 = *mp3_decoder;
    return DEV_OK;
}

void local_playback_close(local_playback_t *player)
{
    if (player == NULL) return;
    if ((player->file != NULL) && (player->fs.close != NULL)) player->fs.close(player->fs.ctx, player->file);
    player->file = NULL;
    player->codec = LOCAL_CODEC_NONE;
    player->playing = false;
    player->eof = false;
    player->source_eof = false;
    player->pending_frames = 0U;
    player->input_valid = 0U;
}

dev_status_t local_playback_open(local_playback_t *player, const char *path)
{
    if ((player == NULL) || (path == NULL)) return DEV_EINVAL;
    local_playback_close(player);
    size_t len = strlen(path);
    if (len >= sizeof(player->path)) return DEV_EOVERFLOW;
    memcpy(player->path, path, len + 1U);
    dev_status_t st = player->fs.open(player->fs.ctx, path, &player->file);
    if (st != DEV_OK) return st;

    if (ext_equals(path, ".wav")) {
        player->codec = LOCAL_CODEC_WAV_PCM;
        st = parse_wav(player);
    } else if (ext_equals(path, ".mp3")) {
        if (player->mp3.decode == NULL) st = DEV_ENOTSUP;
        else {
            player->codec = LOCAL_CODEC_MP3;
            st = prepare_mp3(player);
            if ((st == DEV_OK) && (player->mp3.reset != NULL)) st = player->mp3.reset(player->mp3.ctx);
        }
    } else {
        st = DEV_ENOTSUP;
    }
    if (st != DEV_OK) {
        local_playback_close(player);
        return st;
    }
    player->playing = false;
    player->eof = false;
    player->source_eof = false;
    player->pending_frames = 0U;
    return DEV_OK;
}

dev_status_t local_playback_set_playing(local_playback_t *player, bool playing)
{
    if ((player == NULL) || (player->file == NULL)) return DEV_ENOTREADY;
    if (player->eof && playing) return DEV_ESTATE;
    player->playing = playing;
    return DEV_OK;
}

static dev_status_t flush_pending_pcm(local_playback_t *p)
{
    if (p->pending_frames == 0U) return DEV_OK;
    dev_status_t st = p->sink.write(p->sink.ctx, p->pending_pcm,
                                    p->pending_frames,
                                    p->pending_format.channels,
                                    p->pending_format.sample_rate_hz);
    if (st == DEV_OK) p->pending_frames = 0U;
    return st;
}

static dev_status_t process_wav(local_playback_t *p)
{
    if (p->pending_frames != 0U) {
        dev_status_t st = flush_pending_pcm(p);
        if (st != DEV_OK) return st;
        if (p->data_bytes_remaining == 0U) {
            p->eof = true;
            p->playing = false;
        }
        return DEV_OK;
    }

    uint32_t bytes_per_frame = (uint32_t)p->format.channels * 2U;
    size_t max_bytes = sizeof(p->pending_pcm);
    size_t wanted = (p->data_bytes_remaining < max_bytes) ? p->data_bytes_remaining : max_bytes;
    wanted -= wanted % bytes_per_frame;
    if (wanted == 0U) {
        p->eof = true;
        p->playing = false;
        return DEV_OK;
    }
    size_t got = 0U;
    dev_status_t st = p->fs.read(p->fs.ctx, p->file, p->pending_pcm, wanted, &got);
    if (st != DEV_OK) return st;
    if (got > wanted) return DEV_EIO;
    got -= got % bytes_per_frame;
    if (got == 0U) {
        p->eof = true;
        p->playing = false;
        return DEV_OK;
    }
    p->data_bytes_remaining -= (uint32_t)got;
    p->pending_frames = got / bytes_per_frame;
    p->pending_format = p->format;
    st = flush_pending_pcm(p);
    if ((st == DEV_OK) && (p->data_bytes_remaining == 0U)) {
        p->eof = true;
        p->playing = false;
    }
    return st;
}

static dev_status_t process_mp3(local_playback_t *p)
{
    if (p->pending_frames != 0U) {
        dev_status_t st = flush_pending_pcm(p);
        if (st != DEV_OK) return st;
        return DEV_OK;
    }

    if (p->input_valid < sizeof(p->input_buffer) && !p->source_eof) {
        size_t got = 0U;
        dev_status_t st = p->fs.read(p->fs.ctx, p->file,
                                     p->input_buffer + p->input_valid,
                                     sizeof(p->input_buffer) - p->input_valid, &got);
        if (st != DEV_OK) return st;
        size_t room = sizeof(p->input_buffer) - p->input_valid;
        if (got > room) return DEV_EIO;
        p->input_valid += got;
        if (got == 0U) p->source_eof = true;
        if (p->source_eof && p->input_valid == 0U) {
            p->eof = true;
            p->playing = false;
            return DEV_OK;
        }
    }

    size_t consumed = 0U, frames = 0U;
    local_audio_format_t fmt = {0};
    dev_status_t st = p->mp3.decode(p->mp3.ctx, p->input_buffer, p->input_valid,
                                    &consumed, p->pending_pcm, APP_LOCAL_PCM_FRAMES_PER_CHUNK,
                                    &frames, &fmt);
    if ((consumed > p->input_valid) || (frames > APP_LOCAL_PCM_FRAMES_PER_CHUNK)) return DEV_EIO;
    if (consumed != 0U) {
        memmove(p->input_buffer, p->input_buffer + consumed, p->input_valid - consumed);
        p->input_valid -= consumed;
    }
    if (st != DEV_OK && st != DEV_EBUSY) return st;
    if ((consumed == 0U) && (frames == 0U)) {
        if (p->source_eof) {
            /* A valid MP3 commonly ends with an ID3v1 tag, padding, or a short
             * final fragment which cannot form another frame. Once the source
             * has ended, treat that undecodable tail as normal EOF rather than
             * returning EBUSY forever with an empty audio FIFO. */
            p->input_valid = 0U;
            p->eof = true;
            p->playing = false;
            return DEV_OK;
        }
        if (p->input_valid == sizeof(p->input_buffer)) {
            /* With minimp3's documented 16 KiB look-ahead, a full buffer with
             * no progress is malformed data rather than ordinary starvation. */
            return DEV_EIO;
        }
        return DEV_EBUSY;
    }
    if (st == DEV_EBUSY || frames == 0U) return DEV_EBUSY;
    if ((fmt.channels == 0U) || (fmt.channels > 2U) ||
        (fmt.sample_rate_hz < 8000U) || (fmt.sample_rate_hz > 96000U)) return DEV_EIO;
    p->format = fmt;
    p->pending_frames = frames;
    p->pending_format = fmt;
    return flush_pending_pcm(p);
}

dev_status_t local_playback_process(local_playback_t *player)
{
    if ((player == NULL) || (player->file == NULL)) return DEV_ENOTREADY;
    if (!player->playing) return DEV_OK;
    dev_status_t st;
    if (player->codec == LOCAL_CODEC_WAV_PCM) st = process_wav(player);
    else if (player->codec == LOCAL_CODEC_MP3) st = process_mp3(player);
    else st = DEV_ENOTSUP;

    /* A busy sink is normal back-pressure. Any other processing failure means
     * the current stream cannot safely continue (notably SD hot-removal or a
     * corrupt decoder result). Close it once so the 10 ms application loop
     * does not hammer a stale FatFs object indefinitely. */
    if (st != DEV_OK && st != DEV_EBUSY) local_playback_close(player);
    return st;
}

