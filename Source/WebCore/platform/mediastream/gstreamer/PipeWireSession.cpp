/*
 * Copyright (C) 2024 Igalia S.L
 * SPDX-FileCopyrightText: Copyright © 2018 Wim Taymans
 * SPDX-License-Identifier: MIT
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "PipeWireSession.h"

#include <fcntl.h>
#include <glib.h>
#include <pipewire/context.h>
#include <pipewire/keys.h>
#include <spa/monitor/device.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/parser.h>
#include <spa/utils/defs.h>
#include <spa/utils/dict.h>
#include <spa/utils/result.h>
#include <spa/utils/string.h>

namespace WebCore {

RefPtr<PipeWireSession> PipeWireSession::create(int fd)
{
    pw_init(nullptr, nullptr);

    auto loop = pw_thread_loop_new("pipewire-main-loop", nullptr);
    auto context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    auto core = pw_context_connect_fd(context, fcntl(fd, F_DUPFD_CLOEXEC, 3), nullptr, 0);
    if (!core) {
        WTFLogAlways("Unable to connect to PipeWire");
        pw_context_destroy(context);
        pw_thread_loop_destroy(loop);
        return nullptr;
    }

    return adoptRef(*new PipeWireSession(loop, context, core));
}

PipeWireSession::PipeWireSession(struct pw_thread_loop* loop, struct pw_context* context, struct pw_core* core)
    : m_loop(loop)
    , m_context(context)
    , m_core(core)
{
    static const struct pw_core_events coreEvents = {
        .version = PW_VERSION_CORE_EVENTS,
        .done = [](void* data, uint32_t id, int seq) {
            if (id != PW_ID_CORE)
                return;
            auto session = reinterpret_cast<PipeWireSession*>(data);
            if (seq == session->m_lastSeq) {
                //pw_thread_loop_stop(session->m_loop);
                session->m_loopDone = true;
            }
            pw_thread_loop_signal(session->m_loop, FALSE);
        },
        .error = [](void* data, uint32_t id, int seq, int res, const char* message) {
            auto session = reinterpret_cast<PipeWireSession*>(data);
            WTFLogAlways("PipeWire error id:%u seq:%d res:%d (%s): %s", id, seq, res, spa_strerror(res), message);
            if (id == PW_ID_CORE)
                session->m_lastError = res;

            pw_thread_loop_signal(session->m_loop, FALSE);
        }
    };

    {
        IGNORE_WARNINGS_BEGIN("unused-value");
        pw_core_add_listener(m_core, &m_coreListener, &coreEvents, this);
        IGNORE_WARNINGS_END;
    }

    m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);

    static const struct pw_registry_events registryEvents = {
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = [](void* data, uint32_t nodeId, uint32_t permissions, const char* type, uint32_t version, const struct spa_dict* properties) {
            UNUSED_PARAM(permissions);
            UNUSED_PARAM(version);

            if (!g_str_equal(type, PW_TYPE_INTERFACE_Node))
                return;

            if (!properties)
                return;

            if (!spa_dict_lookup(properties, PW_KEY_NODE_DESCRIPTION))
                return;

            const char* role = spa_dict_lookup(properties, PW_KEY_MEDIA_ROLE);
            if (!role || !g_str_equal(role, "Camera"))
                return;

            auto session = reinterpret_cast<PipeWireSession*>(data);
            session->m_nodes.set(nodeId, makeUnique<PipeWireNode>(nodeId, session->m_registry, session));
            session->sync();
        },
        .global_remove = [](void*, uint32_t) { }
    };

    {
        IGNORE_WARNINGS_BEGIN("unused-value");
        pw_registry_add_listener(m_registry, &m_registryListener, &registryEvents, this);
        IGNORE_WARNINGS_END;
    }
}

PipeWireSession::~PipeWireSession()
{
    pw_thread_loop_stop(m_loop);
    pw_proxy_destroy(reinterpret_cast<struct pw_proxy*>(m_registry));
    pw_core_disconnect(m_core);
    pw_context_destroy(m_context);
    pw_thread_loop_destroy(m_loop);
    pw_deinit();
}

Vector<PipeWireNodeData> PipeWireSession::run()
{
    m_loopDone = false;
    m_pendingSeq = 0;
    sync();

    if (pw_thread_loop_start(m_loop) < 0) {
        WTFLogAlways("Unable to start PipeWire context loop");
        return { };
    }

    while (!m_loopDone) {
        pw_thread_loop_lock(m_loop);
        pw_thread_loop_wait(m_loop);
        while (m_lastSeq != m_pendingSeq && m_lastError >= 0) {
            struct timespec abstime;
            if (pw_thread_loop_timed_wait_full(m_loop, &abstime) < 0)
                break;
        }
        pw_thread_loop_unlock(m_loop);
    }

    Vector<PipeWireNodeData> nodes;
    nodes.reserveInitialCapacity(m_nodes.size());
    for (auto& [id, node] : m_nodes)
        nodes.append(node->data());
    return nodes;
}

void PipeWireSession::sync()
{
    IGNORE_WARNINGS_BEGIN("unused-value");
    m_pendingSeq = pw_core_sync(m_core, PW_ID_CORE, 0);
    IGNORE_WARNINGS_END;
    pw_thread_loop_signal(m_loop, FALSE);
}

void PipeWireSession::finalSync()
{
    IGNORE_WARNINGS_BEGIN("unused-value");
    m_pendingSeq = pw_core_sync(m_core, PW_ID_CORE, 0);
    IGNORE_WARNINGS_END;
    m_lastSeq = m_pendingSeq;
}

static void handleIntProperty(const struct spa_pod_prop* prop, const char* key, GRefPtr<GstCaps>& res)
{
    uint32_t totalItems, choice;
    struct spa_pod* val = spa_pod_get_values(&prop->value, &totalItems, &choice);
    if (val->type != SPA_TYPE_Int)
        return;

    auto ints = static_cast<uint32_t*>(SPA_POD_BODY(val));

    switch (choice) {
    case SPA_CHOICE_None:
        gst_caps_set_simple(res.get(), key, G_TYPE_INT, ints[0], nullptr);
        break;
    case SPA_CHOICE_Range:
    case SPA_CHOICE_Step: {
        if (totalItems < 3)
            return;
        gst_caps_set_simple(res.get(), key, GST_TYPE_INT_RANGE, ints[1], ints[2], nullptr);
        break;
    }
    case SPA_CHOICE_Enum: {
        GValue list = G_VALUE_INIT, v = G_VALUE_INIT;

        g_value_init(&list, GST_TYPE_LIST);
        for (uint32_t i = 1; i < totalItems; i++) {
            g_value_init(&v, G_TYPE_INT);
            g_value_set_int(&v, ints[i]);
            gst_value_list_append_and_take_value(&list, &v);
        }
        gst_caps_set_value(res.get(), key, &list);
        g_value_unset(&list);
        break;
    }
    default:
        break;
    }
}

static void handleRectangleProperty(const struct spa_pod_prop* prop, GRefPtr<GstCaps>& res)
{
    uint32_t totalItems, choice;
    struct spa_pod* val = spa_pod_get_values(&prop->value, &totalItems, &choice);
    if (val->type != SPA_TYPE_Rectangle)
        return;

    auto* rect = static_cast<struct spa_rectangle*>(SPA_POD_BODY(val));

    switch (choice) {
    case SPA_CHOICE_None:
        gst_caps_set_simple(res.get(), "width", G_TYPE_INT, rect[0].width, "height", G_TYPE_INT, rect[0].height, nullptr);
        break;
    case SPA_CHOICE_Range:
    case SPA_CHOICE_Step: {
        if (totalItems < 3)
            return;
        gst_caps_set_simple(res.get(), "width", GST_TYPE_INT_RANGE, rect[1].width, rect[2].width,
            "height", GST_TYPE_INT_RANGE, rect[1].height, rect[2].height, nullptr);
        break;
    }
    case SPA_CHOICE_Enum: {
        GValue l1 = G_VALUE_INIT, l2 = G_VALUE_INIT, v1 = G_VALUE_INIT, v2 = G_VALUE_INIT;

        g_value_init(&l1, GST_TYPE_LIST);
        g_value_init(&l2, GST_TYPE_LIST);
        for (uint32_t i = 1; i < totalItems; i++) {
            g_value_init(&v1, G_TYPE_INT);
            g_value_set_int(&v1, rect[i].width);
            gst_value_list_append_and_take_value(&l1, &v1);

            g_value_init(&v2, G_TYPE_INT);
            g_value_set_int(&v2, rect[i].height);
            gst_value_list_append_and_take_value(&l2, &v2);
        }
        gst_caps_set_value(res.get(), "width", &l1);
        gst_caps_set_value(res.get(), "height", &l2);
        g_value_unset(&l1);
        g_value_unset(&l2);
        break;
    }
    }
}

static void handleFractionProperty(const struct spa_pod_prop* prop, const char* key, GRefPtr<GstCaps>& res)
{
    uint32_t totalItems, choice;
    struct spa_pod* val = spa_pod_get_values(&prop->value, &totalItems, &choice);
    if (val->type != SPA_TYPE_Fraction)
        return;

    auto fract = static_cast<struct spa_fraction*>(SPA_POD_BODY(val));

    switch (choice) {
    case SPA_CHOICE_None:
        gst_caps_set_simple(res.get(), key, GST_TYPE_FRACTION, fract[0].num, fract[0].denom, nullptr);
        break;
    case SPA_CHOICE_Range:
    case SPA_CHOICE_Step: {
        if (totalItems < 3)
            return;
        gst_caps_set_simple(res.get(), key, GST_TYPE_FRACTION_RANGE, fract[1].num, fract[1].denom, fract[2].num, fract[2].denom, nullptr);
        break;
    }
    case SPA_CHOICE_Enum: {
        GValue l1 = G_VALUE_INIT, v1 = G_VALUE_INIT;
        g_value_init(&l1, GST_TYPE_LIST);
        for (uint32_t i = 1; i < totalItems; i++) {
            g_value_init(&v1, GST_TYPE_FRACTION);
            gst_value_set_fraction(&v1, fract[i].num, fract[i].denom);
            gst_value_list_append_and_take_value(&l1, &v1);
        }
        gst_caps_set_value(res.get(), key, &l1);
        g_value_unset(&l1);
        break;
    }
    }
}

static const uint32_t videoFormatMap[] = {
    SPA_VIDEO_FORMAT_UNKNOWN,
    SPA_VIDEO_FORMAT_ENCODED,
    SPA_VIDEO_FORMAT_I420,
    SPA_VIDEO_FORMAT_YV12,
    SPA_VIDEO_FORMAT_YUY2,
    SPA_VIDEO_FORMAT_UYVY,
    SPA_VIDEO_FORMAT_AYUV,
    SPA_VIDEO_FORMAT_RGBx,
    SPA_VIDEO_FORMAT_BGRx,
    SPA_VIDEO_FORMAT_xRGB,
    SPA_VIDEO_FORMAT_xBGR,
    SPA_VIDEO_FORMAT_RGBA,
    SPA_VIDEO_FORMAT_BGRA,
    SPA_VIDEO_FORMAT_ARGB,
    SPA_VIDEO_FORMAT_ABGR,
    SPA_VIDEO_FORMAT_RGB,
    SPA_VIDEO_FORMAT_BGR,
    SPA_VIDEO_FORMAT_Y41B,
    SPA_VIDEO_FORMAT_Y42B,
    SPA_VIDEO_FORMAT_YVYU,
    SPA_VIDEO_FORMAT_Y444,
    SPA_VIDEO_FORMAT_v210,
    SPA_VIDEO_FORMAT_v216,
    SPA_VIDEO_FORMAT_NV12,
    SPA_VIDEO_FORMAT_NV21,
    SPA_VIDEO_FORMAT_GRAY8,
    SPA_VIDEO_FORMAT_GRAY16_BE,
    SPA_VIDEO_FORMAT_GRAY16_LE,
    SPA_VIDEO_FORMAT_v308,
    SPA_VIDEO_FORMAT_RGB16,
    SPA_VIDEO_FORMAT_BGR16,
    SPA_VIDEO_FORMAT_RGB15,
    SPA_VIDEO_FORMAT_BGR15,
    SPA_VIDEO_FORMAT_UYVP,
    SPA_VIDEO_FORMAT_A420,
    SPA_VIDEO_FORMAT_RGB8P,
    SPA_VIDEO_FORMAT_YUV9,
    SPA_VIDEO_FORMAT_YVU9,
    SPA_VIDEO_FORMAT_IYU1,
    SPA_VIDEO_FORMAT_ARGB64,
    SPA_VIDEO_FORMAT_AYUV64,
    SPA_VIDEO_FORMAT_r210,
    SPA_VIDEO_FORMAT_I420_10BE,
    SPA_VIDEO_FORMAT_I420_10LE,
    SPA_VIDEO_FORMAT_I422_10BE,
    SPA_VIDEO_FORMAT_I422_10LE,
    SPA_VIDEO_FORMAT_Y444_10BE,
    SPA_VIDEO_FORMAT_Y444_10LE,
    SPA_VIDEO_FORMAT_GBR,
    SPA_VIDEO_FORMAT_GBR_10BE,
    SPA_VIDEO_FORMAT_GBR_10LE,
    SPA_VIDEO_FORMAT_NV16,
    SPA_VIDEO_FORMAT_NV24,
    SPA_VIDEO_FORMAT_NV12_64Z32,
    SPA_VIDEO_FORMAT_A420_10BE,
    SPA_VIDEO_FORMAT_A420_10LE,
    SPA_VIDEO_FORMAT_A422_10BE,
    SPA_VIDEO_FORMAT_A422_10LE,
    SPA_VIDEO_FORMAT_A444_10BE,
    SPA_VIDEO_FORMAT_A444_10LE,
    SPA_VIDEO_FORMAT_NV61,
    SPA_VIDEO_FORMAT_P010_10BE,
    SPA_VIDEO_FORMAT_P010_10LE,
    SPA_VIDEO_FORMAT_IYU2,
    SPA_VIDEO_FORMAT_VYUY,
    SPA_VIDEO_FORMAT_GBRA,
    SPA_VIDEO_FORMAT_GBRA_10BE,
    SPA_VIDEO_FORMAT_GBRA_10LE,
    SPA_VIDEO_FORMAT_GBR_12BE,
    SPA_VIDEO_FORMAT_GBR_12LE,
    SPA_VIDEO_FORMAT_GBRA_12BE,
    SPA_VIDEO_FORMAT_GBRA_12LE,
    SPA_VIDEO_FORMAT_I420_12BE,
    SPA_VIDEO_FORMAT_I420_12LE,
    SPA_VIDEO_FORMAT_I422_12BE,
    SPA_VIDEO_FORMAT_I422_12LE,
    SPA_VIDEO_FORMAT_Y444_12BE,
    SPA_VIDEO_FORMAT_Y444_12LE,
};

#if __BYTE_ORDER == __BIG_ENDIAN
#define _FORMAT_LE(fmt) SPA_AUDIO_FORMAT_##fmt##_OE
#define _FORMAT_BE(fmt) SPA_AUDIO_FORMAT_##fmt
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#define _FORMAT_LE(fmt) SPA_AUDIO_FORMAT_##fmt
#define _FORMAT_BE(fmt) SPA_AUDIO_FORMAT_##fmt##_OE
#endif

static const uint32_t audioFormatMap[] = {
    SPA_AUDIO_FORMAT_UNKNOWN,
    SPA_AUDIO_FORMAT_ENCODED,
    SPA_AUDIO_FORMAT_S8,
    SPA_AUDIO_FORMAT_U8,
    _FORMAT_LE(S16),
    _FORMAT_BE(S16),
    _FORMAT_LE(U16),
    _FORMAT_BE(U16),
    _FORMAT_LE(S24_32),
    _FORMAT_BE(S24_32),
    _FORMAT_LE(U24_32),
    _FORMAT_BE(U24_32),
    _FORMAT_LE(S32),
    _FORMAT_BE(S32),
    _FORMAT_LE(U32),
    _FORMAT_BE(U32),
    _FORMAT_LE(S24),
    _FORMAT_BE(S24),
    _FORMAT_LE(U24),
    _FORMAT_BE(U24),
    _FORMAT_LE(S20),
    _FORMAT_BE(S20),
    _FORMAT_LE(U20),
    _FORMAT_BE(U20),
    _FORMAT_LE(S18),
    _FORMAT_BE(S18),
    _FORMAT_LE(U18),
    _FORMAT_BE(U18),
    _FORMAT_LE(F32),
    _FORMAT_BE(F32),
    _FORMAT_LE(F64),
    _FORMAT_BE(F64),
};

static std::optional<int> findIndex(const uint32_t* items, int totalItems, uint32_t id)
{
    for (int i = 0; i < totalItems; i++) {
        if (items[i] == id)
            return i;
    }
    return { };
}

static const char* videoIdToString(uint32_t id)
{
    auto index = findIndex(videoFormatMap, SPA_N_ELEMENTS(videoFormatMap), id);
    if (!index)
        return nullptr;
    return gst_video_format_to_string(static_cast<GstVideoFormat>(*index));
}

static const char* audioIdToString(uint32_t id)
{
    auto index = findIndex(audioFormatMap, SPA_N_ELEMENTS(audioFormatMap), id);
    if (!index)
        return nullptr;
    return gst_audio_format_to_string(static_cast<GstAudioFormat>(*index));
}

using IdToString = Function<const char*(uint32_t)>;

static void handleIdProp(const struct spa_pod_prop* prop, const char* key, IdToString func, GRefPtr<GstCaps>& res)
{
    struct spa_pod* val;
    uint32_t i, totalItems, choice;
    val = spa_pod_get_values(&prop->value, &totalItems, &choice);
    if (val->type != SPA_TYPE_Id)
        return;

    auto id = static_cast<uint32_t*>(SPA_POD_BODY(val));
    const char* str;
    switch (choice) {
    case SPA_CHOICE_None:
        str = func(id[0]);
        if (!str)
            return;
        gst_caps_set_simple(res.get(), key, G_TYPE_STRING, str, nullptr);
        break;
    case SPA_CHOICE_Enum: {
        GValue list = G_VALUE_INIT, v = G_VALUE_INIT;

        g_value_init(&list, GST_TYPE_LIST);
        for (i = 1; i < totalItems; i++) {
            str = func(id[i]);
            if (!str)
                continue;

            g_value_init(&v, G_TYPE_STRING);
            g_value_set_string(&v, str);
            gst_value_list_append_and_take_value(&list, &v);
        }
        gst_caps_set_value(res.get(), key, &list);
        g_value_unset(&list);
        break;
    }
    default:
        break;
    }
}

static GRefPtr<GstCaps> spaPodToCaps(const spa_pod* spaPod)
{
    auto* obj = reinterpret_cast<const spa_pod_object*>(spaPod);
    const spa_pod_prop* prop = nullptr;
    uint32_t mediaType, mediaSubType;
    GRefPtr<GstCaps> caps;

    if (spa_format_parse(spaPod, &mediaType, &mediaSubType) < 0)
        return caps;

    if (mediaType == SPA_MEDIA_TYPE_video) {
        if (mediaSubType == SPA_MEDIA_SUBTYPE_raw) {
            caps = adoptGRef(gst_caps_new_empty_simple("video/x-raw"));
            prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_VIDEO_format);
            if (prop)
                handleIdProp(prop, "format", videoIdToString, caps);
        } else if (mediaSubType == SPA_MEDIA_SUBTYPE_mjpg)
            caps = adoptGRef(gst_caps_new_empty_simple("image/jpeg"));
        else if (mediaSubType == SPA_MEDIA_SUBTYPE_h264)
            caps = adoptGRef(gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr));
        else
            return caps;

        prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_VIDEO_size);
        if (prop)
            handleRectangleProperty(prop, caps);

        prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_VIDEO_framerate);
        if (prop)
            handleFractionProperty(prop, "framerate", caps);

        prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_VIDEO_maxFramerate);
        if (prop)
            handleFractionProperty(prop, "max-framerate", caps);
        return caps;
    }

    if (mediaType == SPA_MEDIA_TYPE_audio) {
        if (mediaSubType == SPA_MEDIA_SUBTYPE_raw) {
            caps = gst_caps_new_simple("audio/x-raw", "layout", G_TYPE_STRING, "interleaved", nullptr);
            prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_AUDIO_format);
            if (prop)
                handleIdProp(prop, "format", audioIdToString, caps);

            prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_AUDIO_rate);
            if (prop)
                handleIntProperty(prop, "rate", caps);

            prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_AUDIO_channels);
            if (prop)
                handleIntProperty(prop, "channels", caps);
        }
    }

    return caps;
}

PipeWireNode::PipeWireNode(uint32_t nodeId, struct pw_registry* registry, RefPtr<PipeWireSession>&& session)
    : m_session(WTFMove(session))
    , m_data(PipeWireNodeData(nodeId))
{
    m_proxy = static_cast<pw_proxy*>(pw_registry_bind(registry, nodeId, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));

    static const pw_node_events nodeEvents {
        .version = PW_VERSION_NODE_EVENTS,
        .info = [](void* data, const pw_node_info* info) {
            PipeWireNode* self = static_cast<PipeWireNode*>(data);

            if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
                self->m_data.persistentId = makeString(spa_dict_lookup(info->props, SPA_KEY_DEVICE_VENDOR_ID), '-', spa_dict_lookup(info->props, SPA_KEY_DEVICE_PRODUCT_ID), '-', spa_dict_lookup(info->props, PW_KEY_DEVICE_ID));
                self->m_data.label = makeString(spa_dict_lookup(info->props, PW_KEY_NODE_DESCRIPTION));
                for (uint32_t i = 0; i < info->n_params; i++) {
                    uint32_t id = info->params[i].id;
                    if (id == SPA_PARAM_EnumFormat && info->params[i].flags & SPA_PARAM_INFO_READ) {
                        IGNORE_WARNINGS_BEGIN("unused-value");
                        pw_node_enum_params(self->m_proxy, 0, id, 0, UINT32_MAX, nullptr);
                        IGNORE_WARNINGS_END;
                        break;
                    }
                }
                self->m_session->finalSync();
            }
        },
        .param = [](void* data, int, uint32_t, uint32_t, uint32_t, const spa_pod* spaPod) {
            PipeWireNode* self = static_cast<PipeWireNode*>(data);
            auto caps = spaPodToCaps(spaPod);
            if (!self->m_data.caps)
                self->m_data.caps = WTFMove(caps);
            else
                gst_caps_append(self->m_data.caps.get(), caps.leakRef());
        },
    };

    IGNORE_WARNINGS_BEGIN("unused-value");
    pw_node_add_listener(m_proxy, &m_nodeListener, &nodeEvents, this);
    IGNORE_WARNINGS_END;
}

PipeWireNode::~PipeWireNode()
{
    pw_proxy_destroy(m_proxy);
    spa_hook_remove(&m_nodeListener);
}

} // namespace WebCore
