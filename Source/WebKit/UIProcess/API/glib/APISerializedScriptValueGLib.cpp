/*
 * Copyright (C) 2022 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "APISerializedScriptValue.h"

#include <JavaScriptCore/APICast.h>
#include <JavaScriptCore/JSBase.h>
#include <JavaScriptCore/JSContextPrivate.h>
#include <JavaScriptCore/JSGlobalObjectInlines.h>
#include <JavaScriptCore/JSRemoteInspector.h>
// #include <jsc/JSCContextPrivate.h>
// #include <jsc/JSCValuePrivate.h>
#include <jsc/jsc.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>
#include <wtf/glib/GRefPtr.h>

namespace API {

static constexpr auto sharedJSContextMaxIdleTime = 10_s;

class SharedJSContext {
public:
    SharedJSContext()
        : m_timer(RunLoop::main(), this, &SharedJSContext::releaseContextIfNecessary)
    {
    }

    JSCContext* ensureContext()
    {
        m_lastUseTime = MonotonicTime::now();
        if (!m_context) {
            bool previous = JSRemoteInspectorGetInspectionEnabledByDefault();
            JSRemoteInspectorSetInspectionEnabledByDefault(false);
            m_context = adoptGRef(jsc_context_new());
            JSRemoteInspectorSetInspectionEnabledByDefault(previous);

            m_timer.startOneShot(sharedJSContextMaxIdleTime);
        }
        return m_context.get();
    }

    void releaseContextIfNecessary()
    {
        auto idleTime = MonotonicTime::now() - m_lastUseTime;
        if (idleTime < sharedJSContextMaxIdleTime) {
            // We lazily restart the timer if needed every 10 seconds instead of doing so every time ensureContext()
            // is called, for performance reasons.
            m_timer.startOneShot(sharedJSContextMaxIdleTime - idleTime);
            return;
        }
        m_context.clear();
    }

private:
    GRefPtr<JSCContext> m_context;
    RunLoop::Timer<SharedJSContext> m_timer;
    MonotonicTime m_lastUseTime;
};

static SharedJSContext& sharedContext()
{
    static NeverDestroyed<SharedJSContext> sharedContext;
    return sharedContext.get();
}

static bool validateObject(GVariant* argument)
{
    return true;
#if 0
if ([argument isKindOfClass:[NSString class]] || [argument isKindOfClass:[NSNumber class]] || [argument isKindOfClass:[NSDate class]] || [argument isKindOfClass:[NSNull class]])
        return true;

    if ([argument isKindOfClass:[NSArray class]]) {
        __block BOOL valid = true;

        [argument enumerateObjectsUsingBlock:^(id object, NSUInteger, BOOL *stop) {
            if (!validateObject(object)) {
                valid = false;
                *stop = YES;
            }
        }];

        return valid;
    }

    if ([argument isKindOfClass:[NSDictionary class]]) {
        __block bool valid = true;

        [argument enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *stop) {
            if (!validateObject(key) || !validateObject(value)) {
                valid = false;
                *stop = YES;
            }
        }];

        return valid;
    }

    return false;
#endif
}

static RefPtr<WebCore::SerializedScriptValue> coreValueFromGVariant(GVariant* object)
{
    if (object && !validateObject(object))
        return nullptr;

#if 0
    ASSERT(RunLoop::isMain());
    auto* context = sharedContext().ensureContext();
    auto* value = jsc_value_new_number(context, 42);
    if (!value)
        return nullptr;
    auto globalObject = toJS(jscContextGetJSContext(context));
    ASSERT(globalObject);
    JSC::JSLockHolder lock(globalObject);

    return WebCore::SerializedScriptValue::create(*globalObject, toJS(globalObject, jscValueGetJSValue(value)));
#else
    return nullptr;
#endif
}

RefPtr<SerializedScriptValue> SerializedScriptValue::createFromGVariant(GVariant* object)
{
    auto coreValue = coreValueFromGVariant(object);
    if (!coreValue)
        return nullptr;
    return create(coreValue.releaseNonNull());
}

}; // namespace API
