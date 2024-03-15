/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019,
                2020, 2021, 2022, 2023 Vladimír Vondruš <mosra@centrum.cz>
    Copyright © 2018, 2019, 2020 Jonathan Hale <squareys@googlemail.com>
    Copyright © 2020 Pablo Escobar <mail@rvrs.in>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include "EmscriptenApplication.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Containers/ScopeGuard.h>
#include <Corrade/Containers/StringView.h>
#include <Corrade/Utility/Arguments.h>
#include <Corrade/Utility/Debug.h>

#include "Magnum/Math/ConfigurationValue.h"
#include "Magnum/Platform/ScreenedApplication.hpp"
#include "Magnum/Platform/Implementation/DpiScaling.h"

#ifdef MAGNUM_TARGET_GL
#include "Magnum/GL/Version.h"
#endif

#ifdef CORRADE_TARGET_EMSCRIPTEN
/* Implemented in Platform.js.in */
extern "C" {
    char* magnumPlatformCanvasId();
    char* magnumPlatformKeyboardListeningElement();
    void magnumPlatformSetWindowTitle(const char* string, std::size_t size);
    void magnumPlatformSetContainerCssClass(const char* string, std::size_t size);
    void magnumPlatformSetCursor(const char* string, std::size_t size);
    void magnumPlatformRequestAnimationFrame(int(*callback)(void*), void* state);
}
#endif

namespace Magnum { namespace Platform {

using namespace Containers::Literals;

enum class EmscriptenApplication::Flag: UnsignedByte {
    Redraw = 1 << 0,
    TextInputActive = 1 << 1,
    ExitRequested = 1 << 2,
    LoopActive = 1 << 3
};

namespace {
    typedef EmscriptenApplication::KeyEvent::Key Key;

    /* Entry for key name to `Key` enum mapping */
    struct Entry {
        const char* key;
        Key value;
    };

    /* Key name to `Key` enum mapping. These need to be sorted for use with
       std::lower_bound in KeyEvent::toKey */
    constexpr Entry KeyMapping[]{
        {"AltLeft", Key::LeftAlt},
        {"AltRight", Key::RightAlt},
        {"ArrowDown", Key::Down},
        {"ArrowLeft", Key::Left},
        {"ArrowRight", Key::Right},
        {"ArrowUp", Key::Up},
        {"Backquote", Key::Backquote},
        {"Backslash", Key::Backslash},
        {"Backspace", Key::Backspace},
        {"BracketLeft", Key::LeftBracket},
        {"BracketRight", Key::RightBracket},
        {"CapsLock", Key::CapsLock},
        {"Comma", Key::Comma},
        {"ContextMenu", Key::Menu},
        {"ControlLeft", Key::LeftCtrl},
        {"ControlRight", Key::RightCtrl},
        {"Delete", Key::Delete},
        {"End", Key::End},
        {"Enter", Key::Enter},
        {"Equal", Key::Equal},
        {"Escape", Key::Esc},
        {"Home", Key::Home},
        {"Insert", Key::Insert},
        {"MetaLeft", Key::LeftSuper},
        {"MetaRight", Key::RightSuper},
        {"Minus", Key::Minus},
        {"NumLock", Key::NumLock},
        {"PageDown", Key::PageDown},
        {"PageUp", Key::PageUp},
        {"Pause", Key::Pause},
        {"Period", Key::Period},
        {"Plus", Key::Plus},
        {"PrintScreen", Key::PrintScreen},
        {"Quote", Key::Quote},
        {"ScrollLock", Key::ScrollLock},
        {"Semicolon", Key::Semicolon},
        {"ShiftLeft", Key::LeftShift},
        {"ShiftRight", Key::RightShift},
        {"Slash", Key::Slash},
        {"Space", Key::Space},
        {"Tab", Key::Tab},
    };

    /* Predicate for Entry "less than" to use with std::lower_bound */
    struct EntryCompare {
        bool operator()(const Entry& a, const char* const b) {
            return std::strcmp(a.key, b) < 0;
        }

        bool operator()(const char* const a, const Entry& b) {
            return std::strcmp(a, b.key) < 0;
        }
    };

    /* Translate emscripten key code (as defined by
       https://www.w3.org/TR/uievents-code/#key-code-attribute-value)
       to Key enum. `key` is a keyboard layout dependent key string, e.g. 'a',
       or '-'; `code` is a keyboard layout independent key string, e.g. 'KeyA'
       or 'Minus'. Note that the Y key on some layouts may result in 'KeyZ'. */
    Key toKey(const EM_UTF8* const key, const EM_UTF8* const code) {
        const Containers::StringView keyView = key;
        if(keyView.isEmpty()) return Key::Unknown;

        /* We use key for a-z as it gives us a keyboard layout respecting
           representation of the key, i.e. we get `z` for z depending on layout
           where code may give us `y` independent of the layout. */
        if(keyView.size() == 1) {
            if(key[0] >= 'a' && key[0] <= 'z') return Key(key[0]);
            else if(key[0] >= 'A' && key[0] <= 'Z') return Key(key[0] - 'A' + 'a');
        }

        /* We use code for 0-9 as it allows us to differentiate towards Numpad
           digits. For digits independent of numpad or not, key is e.g. '0' for
           Zero */
        const Containers::StringView codeView = code;
        using namespace Containers::Literals;
        constexpr Containers::StringView numpad = "Numpad"_s;
        if(codeView.hasPrefix("Digit"_s)) {
            return Key(code[5]);

        /* Numpad keys */
        } else if(codeView.hasPrefix(numpad)) {
            const Containers::StringView numKey = codeView.exceptPrefix(numpad.size());
            if(numKey == "Add"_s) return Key::NumAdd;
            if(numKey == "Decimal"_s) return Key::NumDecimal;
            if(numKey == "Divide"_s) return Key::NumDivide;
            if(numKey == "Enter"_s) return Key::NumEnter;
            if(numKey == "Equal"_s) return Key::NumEqual;
            if(numKey == "Multiply"_s) return Key::NumMultiply;
            if(numKey == "Subtract"_s) return Key::NumSubtract;

            /* Numpad0 - Numpad9 */
            const Int num = numKey[0] - '0';
            if(num >= 0 && num <= 9) return Key(num + Int(Key::NumZero));

            return Key::Unknown;
        }

        const Containers::ArrayView<const Entry> mapping = KeyMapping;
        const Entry* found = std::lower_bound(mapping.begin(), mapping.end(), code, EntryCompare{});
        if(found != mapping.end() && std::strcmp(found->key, code) == 0)
            return found->value;

        /* F1 - F12 */
        if(code[0] == 'F') {
            /* F1-F9 */
            if(code[2] != '\0') {
                const Int num = code[2] - '0';
                return Key(Int(Key::F10) + num);
            }
            /* F10-F12 */
            const Int num = code[1] - '1';
            return Key(Int(Key::F1) + num);
        }

        return Key::Unknown;
    }

    Containers::String canvasId() {
        return Containers::String{magnumPlatformCanvasId(), [](char* data, std::size_t) { std::free(data); }};
    }
}

EmscriptenApplication::EmscriptenApplication(const Arguments& arguments): EmscriptenApplication{arguments, Configuration{}} {}

EmscriptenApplication::EmscriptenApplication(const Arguments& arguments, const Configuration& configuration): EmscriptenApplication{arguments, NoCreate} {
    create(configuration);
}

#ifdef MAGNUM_TARGET_GL
EmscriptenApplication::EmscriptenApplication(const Arguments& arguments, const Configuration& configuration, const GLConfiguration& glConfiguration): EmscriptenApplication{arguments, NoCreate} {
    create(configuration, glConfiguration);
}
#endif

EmscriptenApplication::EmscriptenApplication(const Arguments& arguments, NoCreateT):
    _flags{Flag::Redraw}
{
    Utility::Arguments args{Implementation::windowScalingArguments()};
    #ifdef MAGNUM_TARGET_GL
    _context.emplace(NoCreate, args, arguments.argc, arguments.argv);
    #else
    /** @todo this is duplicated here, in Sdl2Application and in
        GlfwApplication, figure out a nice non-duplicated way to handle this */
    args.addOption("log", "default").setHelp("log", "console logging", "default|quiet|verbose")
        .setFromEnvironment("log")
        .parse(arguments.argc, arguments.argv);
    #endif

    /* Save command-line arguments */
    if(args.value("log") == "verbose") _verboseLog = true;
    const Containers::StringView dpiScaling = args.value<Containers::StringView>("dpi-scaling");

    /* Use physical DPI scaling */
    if(dpiScaling == "default"_s || dpiScaling == "physical"_s) {

    /* Use explicit dpi scaling vector */
    } else if(dpiScaling.containsAny(" \t\n"_s))
        _commandLineDpiScaling = args.value<Vector2>("dpi-scaling"_s);

    /* Use explicit dpi scaling scalar */
    else
        _commandLineDpiScaling = Vector2{args.value<Float>("dpi-scaling"_s)};
}

EmscriptenApplication::~EmscriptenApplication() {
    #ifdef MAGNUM_TARGET_GL
    /* Destroy Magnum context first to avoid it potentially accessing the
       now-destroyed GL context after */
    _context = Containers::NullOpt;

    emscripten_webgl_destroy_context(_glContext);
    #endif
}

void EmscriptenApplication::create() {
    create(Configuration{});
}

void EmscriptenApplication::create(const Configuration& configuration) {
    if(!tryCreate(configuration)) exit(1);
}

#ifdef MAGNUM_TARGET_GL
void EmscriptenApplication::create(const Configuration& configuration, const GLConfiguration& glConfiguration) {
    if(!tryCreate(configuration, glConfiguration)) exit(32);
}
#endif

Vector2 EmscriptenApplication::dpiScaling(const Configuration& configuration) const {
    return dpiScalingInternal(configuration.dpiScaling());
}

Vector2 EmscriptenApplication::dpiScalingInternal(const Vector2& configurationDpiScaling) const {
    std::ostream* verbose = _verboseLog ? Debug::output() : nullptr;

    /* Use values from the configuration only if not overridden on command line.
       In any case explicit scaling has a precedence before the policy. */
    if(!_commandLineDpiScaling.isZero()) {
        Debug{verbose} << "Platform::EmscriptenApplication: user-defined DPI scaling" << _commandLineDpiScaling;
        return _commandLineDpiScaling;
    } else if(!configurationDpiScaling.isZero()) {
        Debug{verbose} << "Platform::EmscriptenApplication: app-defined DPI scaling" << configurationDpiScaling;
        return configurationDpiScaling;
    }

    /* Unlike Sdl2Application, not taking device pixel ratio into account
       because here we have window size different from framebuffer size.
       However, in order to actually calculate the framebuffer size we need to
       query the device pixel ratio. That's done in tryCreate() below, here it
       is returning 1.0 to be consistent with behavior on other platforms where
       it's either windowSize == framebufferSize and dpiScaling of any value,
       or windowSize != framebufferSize and dpiScaling == 1. */
    return Vector2{1.0f};
}

bool EmscriptenApplication::tryCreate(const Configuration& configuration) {
    #ifdef MAGNUM_TARGET_GL
    if(!(configuration.windowFlags() & Configuration::WindowFlag::Contextless)) {
        return tryCreate(configuration, GLConfiguration{});
    }
    #endif

    std::ostream* verbose = _verboseLog ? Debug::output() : nullptr;

    _canvasTarget = canvasId();

    /* Get CSS canvas size and device pixel ratio and cache it. This is used
       later to detect canvas resizes in emscripten_set_resize_callback() and
       fire viewport events, because browsers are only required to fire resize
       events on the window and not on particular DOM elements. */
    _lastKnownCanvasSize = windowSize();
    _lastKnownDevicePixelRatio = devicePixelRatio();

    /* By default Emscripten creates a 300x150 canvas. That's so freaking
       random I'm getting mad. Use the real (CSS pixels) canvas size instead,
       if the size is not hardcoded from the configuration. This is then
       multiplied by the DPI scaling. */
    Vector2i canvasSize;
    if(!configuration.size().isZero()) {
        canvasSize = configuration.size();
        /* Because hardcoding canvas size for WebGL is usually a wrong thing to
           do, notify about that in the verbose output */
        Debug{verbose} << "Platform::EmscriptenApplication::tryCreate(): hardcoded canvas size" << canvasSize;
    } else {
        canvasSize = _lastKnownCanvasSize;
        Debug{verbose} << "Platform::EmscriptenApplication::tryCreate(): autodetected canvas size" << canvasSize;
    }

    /* Save DPI scaling value from configuration for future use. Device pixel
       ratio together with DPI scaling (which is 1.0 by default) defines
       framebuffer size. See class docs for why it's done like that. */
    _configurationDpiScaling = configuration.dpiScaling();
    Debug{verbose} << "Platform::EmscriptenApplication: device pixel ratio" << _lastKnownDevicePixelRatio.x();
    const Vector2i scaledCanvasSize = canvasSize*dpiScaling(configuration)*_lastKnownDevicePixelRatio;
    emscripten_set_canvas_element_size(_canvasTarget.data(), scaledCanvasSize.x(), scaledCanvasSize.y());

    setupCallbacks(!!(configuration.windowFlags() & Configuration::WindowFlag::Resizable));
    setupAnimationFrame(!!(configuration.windowFlags() & Configuration::WindowFlag::AlwaysRequestAnimationFrame));

    return true;
}

#ifdef MAGNUM_TARGET_GL
bool EmscriptenApplication::tryCreate(const Configuration& configuration, const GLConfiguration& glConfiguration) {
    CORRADE_ASSERT(!(configuration.windowFlags() & Configuration::WindowFlag::Contextless),
        "Platform::EmscriptenApplication::tryCreate(): cannot pass Configuration::WindowFlag::Contextless when creating an OpenGL context", false);
    CORRADE_ASSERT(_context->version() == GL::Version::None,
        "Platform::EmscriptenApplication::tryCreate(): window with OpenGL context already created", false);

    /* Create emscripten WebGL context */
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = glConfiguration.colorBufferSize().a() > 0;
    attrs.depth = glConfiguration.depthBufferSize() > 0;
    attrs.stencil = glConfiguration.stencilBufferSize() > 0;
    attrs.antialias = glConfiguration.sampleCount() > 0;

    attrs.premultipliedAlpha =
        !!(glConfiguration.flags() & GLConfiguration::Flag::PremultipliedAlpha);
    attrs.preserveDrawingBuffer =
        !!(glConfiguration.flags() & GLConfiguration::Flag::PreserveDrawingBuffer);
    if(glConfiguration.flags() & GLConfiguration::Flag::PowerPreferenceLowPower)
        attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_LOW_POWER;
    else if(glConfiguration.flags() & GLConfiguration::Flag::PowerPreferenceHighPerformance)
        attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    else
        attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
    attrs.explicitSwapControl =
        !!(glConfiguration.flags() & GLConfiguration::Flag::ExplicitSwapControl);
    attrs.failIfMajorPerformanceCaveat =
        !!(glConfiguration.flags() & GLConfiguration::Flag::FailIfMajorPerformanceCaveat);
    attrs.enableExtensionsByDefault =
        !!(glConfiguration.flags() & GLConfiguration::Flag::EnableExtensionsByDefault);

    #ifdef MAGNUM_TARGET_GLES2 /* WebGL 1 */
    attrs.majorVersion = 1;
    #else                      /* WebGL 2 */
    attrs.majorVersion = 2;
    #endif

    std::ostream* verbose = _verboseLog ? Debug::output() : nullptr;

    /* Get the canvas ID from Module.canvas, either set by EmscriptenApplication.js
       or overridden/manually set by the user. */
    _canvasTarget = canvasId();

    /* Get CSS canvas size and device pixel ratio and cache it. This is used
       later to detect canvas resizes in emscripten_set_resize_callback() and
       fire viewport events, because browsers are only required to fire resize
       events on the window and not on particular DOM elements. */
    _lastKnownCanvasSize = windowSize();
    _lastKnownDevicePixelRatio = devicePixelRatio();

    /* By default Emscripten creates a 300x150 canvas. That's so freaking
       random I'm getting mad. Use the real (CSS pixels) canvas size instead,
       if the size is not hardcoded from the configuration. This is then
       multiplied by the DPI scaling. */
    Vector2i canvasSize;
    if(!configuration.size().isZero()) {
        canvasSize = configuration.size();
        /* Because hardcoding canvas size for WebGL is usually a wrong thing to
           do, notify about that in the verbose output */
        Debug{verbose} << "Platform::EmscriptenApplication::tryCreate(): hardcoded canvas size" << canvasSize;
    } else {
        canvasSize = _lastKnownCanvasSize;
        Debug{verbose} << "Platform::EmscriptenApplication::tryCreate(): autodetected canvas size" << canvasSize;
    }

    /* Save DPI scaling value from configuration for future use. Device pixel
       ratio together with DPI scaling (which is 1.0 by default) defines
       framebuffer size. See class docs for why it's done like that. */
    _configurationDpiScaling = configuration.dpiScaling();
    Debug{verbose} << "Platform::EmscriptenApplication: device pixel ratio" << _lastKnownDevicePixelRatio.x();
    const Vector2i scaledCanvasSize = canvasSize*dpiScaling(configuration)*_lastKnownDevicePixelRatio;
    emscripten_set_canvas_element_size(_canvasTarget.data(), scaledCanvasSize.x(), scaledCanvasSize.y());

    /* Create WebGL context */
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context(_canvasTarget.data(), &attrs);
    if(!context) {
        /* When context creation fails, `context` is a negative integer
           matching EMSCRIPTEN_RESULT_* defines */
        Error{} << "Platform::EmscriptenApplication::tryCreate(): cannot create WebGL context (EMSCRIPTEN_RESULT"
                << context << Debug::nospace << ")";
        return false;
    }

    /* Make the context current */
    CORRADE_INTERNAL_ASSERT_OUTPUT(emscripten_webgl_make_context_current(_glContext = context) == EMSCRIPTEN_RESULT_SUCCESS);

    setupCallbacks(!!(configuration.windowFlags() & Configuration::WindowFlag::Resizable));
    setupAnimationFrame(!!(configuration.windowFlags() & Configuration::WindowFlag::AlwaysRequestAnimationFrame));

    /* Return true if the initialization succeeds */
    return _context->tryCreate(glConfiguration);
}
#endif

Vector2i EmscriptenApplication::windowSize() const {
    Vector2d size;
    emscripten_get_element_css_size(_canvasTarget.data(), &size.x(), &size.y());
    return Vector2i{Math::round(size)};
}

#ifdef MAGNUM_TARGET_GL
Vector2i EmscriptenApplication::framebufferSize() const {
    Vector2i size;
    emscripten_get_canvas_element_size(_canvasTarget.data(), &size.x(), &size.y());
    return size;
}
#endif

Vector2 EmscriptenApplication::dpiScaling() const {
    return dpiScalingInternal(_configurationDpiScaling);
}

Vector2 EmscriptenApplication::devicePixelRatio() const {
    return Vector2{Float(emscripten_get_device_pixel_ratio())};
}

void EmscriptenApplication::setWindowTitle(const Containers::StringView title) {
    magnumPlatformSetWindowTitle(title.data(), title.size());
}

void EmscriptenApplication::setContainerCssClass(const Containers::StringView cssClass) {
    magnumPlatformSetContainerCssClass(cssClass.data(), cssClass.size());

    /* Trigger a potential viewport event -- we don't poll the canvas size like
       Sdl2Application does, so it needs to be done explicitly */
    handleCanvasResize(nullptr);
}

void EmscriptenApplication::swapBuffers() {
    emscripten_webgl_commit_frame();
}

/* Called from window resize event but also explicitly from
   setContainerCssClass() */
void EmscriptenApplication::handleCanvasResize(const EmscriptenUiEvent* event) {
    const Vector2i canvasSize{windowSize()};
    const Vector2 devicePixelRatio = this->devicePixelRatio();
    if(canvasSize != _lastKnownCanvasSize || devicePixelRatio != _lastKnownDevicePixelRatio) {
        _lastKnownCanvasSize = canvasSize;
        _lastKnownDevicePixelRatio = devicePixelRatio;
        const Vector2 dpiScaling = this->dpiScaling();
        const Vector2i size = canvasSize*dpiScaling*devicePixelRatio;
        emscripten_set_canvas_element_size(_canvasTarget.data(), size.x(), size.y());
        ViewportEvent e{event, canvasSize,
            #ifdef MAGNUM_TARGET_GL
            framebufferSize(),
            #endif
            dpiScaling, devicePixelRatio};
        viewportEvent(e);

        /* Can't say just _flags | Flag::Redraw because in case the
           requestAnimationFrame callback is not set up at the moment it would
           never up this change. */
        redraw();
    }
}

void EmscriptenApplication::setupCallbacks(bool resizable) {
    /* Since 1.38.17 all emscripten_set_*_callback() are macros. Play it safe
       and wrap all lambdas in () to avoid the preprocessor getting upset when
       seeing commas */

    /* Set up the resize callback. Because browsers are only required to fire
       resize events on the window and not on particular DOM elements, we need
       to cache the last known canvas size and fire the event only if that
       changes. Better than polling for this change in every frame like
       Sdl2Application does, but still not ideal. */
    if(resizable) {
        const char* target = EMSCRIPTEN_EVENT_TARGET_WINDOW;
        auto cb = [](int, const EmscriptenUiEvent* event, void* userData) -> Int {
            static_cast<EmscriptenApplication*>(userData)->handleCanvasResize(event);
            return false; /** @todo what does ignoring a resize event mean? */
        };
        emscripten_set_resize_callback(target, this, false, cb);
    }

    emscripten_set_mousedown_callback(_canvasTarget.data(), this, false,
        ([](int, const EmscriptenMouseEvent* event, void* userData) -> Int {
            MouseEvent e{*event};
            static_cast<EmscriptenApplication*>(userData)->mousePressEvent(e);
            return e.isAccepted();
        }));

    emscripten_set_mouseup_callback(_canvasTarget.data(), this, false,
        ([](int, const EmscriptenMouseEvent* event, void* userData) -> Int {
            MouseEvent e{*event};
            static_cast<EmscriptenApplication*>(userData)->mouseReleaseEvent(e);
            return e.isAccepted();
        }));

    emscripten_set_mousemove_callback(_canvasTarget.data(), this, false,
        ([](int, const EmscriptenMouseEvent* event, void* userData) -> Int {
            auto& app = *static_cast<EmscriptenApplication*>(userData);
            /* Relies on the target being the canvas, which should be always
               true for mouse events */
            Vector2i position{Int(event->targetX), Int(event->targetY)};
            MouseMoveEvent e{*event,
                /* Avoid bogus offset at first -- report 0 when the event is
                   called for the first time. */
                app._previousMouseMovePosition == Vector2i{-1} ? Vector2i{} :
                position - app._previousMouseMovePosition};
            app._previousMouseMovePosition = position;
            static_cast<EmscriptenApplication*>(userData)->mouseMoveEvent(e);
            return e.isAccepted();
        }));

    emscripten_set_wheel_callback(_canvasTarget.data(), this, false,
        ([](int, const EmscriptenWheelEvent* event, void* userData) -> Int {
            MouseScrollEvent e{*event};
            static_cast<EmscriptenApplication*>(userData)->mouseScrollEvent(e);
            return e.isAccepted();
        }));

    /* document and window are 'specialEventTargets' in emscripten, matching
       EMSCRIPTEN_EVENT_TARGET_DOCUMENT and EMSCRIPTEN_EVENT_TARGET_WINDOW.
       As the lookup happens with the passed parameter and arrays support
       element lookup via strings, we can unify the code by returning string of
       1 or 2 if the target is document or window. */
    char* const keyboardListeningElement = magnumPlatformKeyboardListeningElement();

    /* If the element is a heap-allocated string, ensure it gets properly freed
       after */
    Containers::ScopeGuard keyboardListeningElementStorage{NoCreate};
    if(keyboardListeningElement &&
       keyboardListeningElement != EMSCRIPTEN_EVENT_TARGET_DOCUMENT &&
       keyboardListeningElement != EMSCRIPTEN_EVENT_TARGET_WINDOW)
    {
        keyboardListeningElementStorage = Containers::ScopeGuard{keyboardListeningElement, std::free};
    }

    /* Happens only if keyboardListeningElement was set, but wasn't a document
       or a window and did not have an `id` attribute. */
    CORRADE_ASSERT(keyboardListeningElement,
        "EmscriptenApplication::setupCallbacks(): invalid value for Module['keyboardListeningElement']", );

    /* keypress_callback does not fire for most of the keys and the modifiers
       don't seem to work, keydown on the other hand works fine for all */
    emscripten_set_keydown_callback(keyboardListeningElement, this, false,
        ([](int, const EmscriptenKeyboardEvent* event, void* userData) -> Int {
            EmscriptenApplication& app = *static_cast<EmscriptenApplication*>(userData);
            const Containers::StringView key = event->key;
            KeyEvent e{*event};
            app.keyPressEvent(e);
            bool accepted = e.isAccepted();

            /* If the key name is a single letter or a start of an UTF-8
               sequence, pass it to the text input event as well. Both SDL and
               GLFW emit key press first and text input after, do it in the
               same order here. */
            if(app.isTextInputActive() && key.size() == 1 || (key.size() >= 1 && UnsignedByte(key[0]) > 127)) {
                TextInputEvent te{*event, key};
                app.textInputEvent(te);
                accepted = accepted || te.isAccepted();
            }

            /* Accepting either the key event, the text input event, or both
               should stop it from propagating further */
            return accepted;
        }));

    emscripten_set_keyup_callback(keyboardListeningElement, this, false,
        ([](int, const EmscriptenKeyboardEvent* event, void* userData) -> Int {
            KeyEvent e{*event};
            static_cast<EmscriptenApplication*>(userData)->keyReleaseEvent(e);
            return e.isAccepted();
        }));
}

void EmscriptenApplication::setupAnimationFrame(bool forceAnimationFrame) {
    if(forceAnimationFrame) {
        _callback = [](void* userData) -> int {
            auto& app = *static_cast<EmscriptenApplication*>(userData);

            if(app._flags & Flag::ExitRequested) {
                app._flags &= ~Flag::LoopActive;
                return false;
            }

            if(app._flags & Flag::Redraw) {
                app._flags &= ~Flag::Redraw;
                app.drawEvent();
            }

            return true;
        };
    } else {
        _callback = [](void* userData) -> int {
            auto& app = *static_cast<EmscriptenApplication*>(userData);

            if((app._flags & Flag::Redraw) && !(app._flags & Flag::ExitRequested)) {
                app._flags &= ~Flag::Redraw;
                app.drawEvent();
            }

            /* If redraw is requested, we will not cancel the already requested
               animation frame. If ForceAnimationFrame is set, we will request
               an animation frame even if redraw is not requested. */
            if((app._flags & Flag::Redraw) && !(app._flags & Flag::ExitRequested)) {
                return true;
            }

            /* Cancel last requested animation frame and make redraw()
               requestAnimationFrame again next time */
            app._flags &= ~Flag::LoopActive;
            return false;
        };
    }
}

namespace {

constexpr Containers::StringView CursorMap[]{
    "auto"_s,
    "default"_s,
    "none"_s,
    "context-menu"_s,
    "help"_s,
    "pointer"_s,
    "progress"_s,
    "wait"_s,
    "cell"_s,
    "crosshair"_s,
    "text"_s,
    "vertical-text"_s,
    "alias"_s,
    "copy"_s,
    "move"_s,
    "no-drop"_s,
    "not-allowed"_s,
    "grab"_s,
    "grabbing"_s,
    "all-scroll"_s,
    "col-resize"_s,
    "row-resize"_s,
    "n-resize"_s,
    "e-resize"_s,
    "s-resize"_s,
    "w-resize"_s,
    "ne-resize"_s,
    "nw-resize"_s,
    "se-resize"_s,
    "sw-resize"_s,
    "ew-resize"_s,
    "ns-resize"_s,
    "nesw-resize"_s,
    "nwse-resize"_s,
    "zoom-in"_s,
    "zoom-out"_s
};

}

void EmscriptenApplication::setCursor(Cursor cursor) {
    _cursor = cursor;
    CORRADE_INTERNAL_ASSERT(UnsignedInt(cursor) < Containers::arraySize(CursorMap));
    magnumPlatformSetCursor(CursorMap[UnsignedInt(cursor)].data(),
                            CursorMap[UnsignedInt(cursor)].size());
}

EmscriptenApplication::Cursor EmscriptenApplication::cursor() {
    return _cursor;
}

bool EmscriptenApplication::isTextInputActive() const {
    return !!(_flags & Flag::TextInputActive);
}

void EmscriptenApplication::startTextInput() {
    _flags |= Flag::TextInputActive;
}

void EmscriptenApplication::stopTextInput() {
    _flags &= ~Flag::TextInputActive;
}

void EmscriptenApplication::setTextInputRect(const Range2Di&) {
    /** @todo place a hidden input field at given rect */
}

void EmscriptenApplication::viewportEvent(ViewportEvent&) {}
void EmscriptenApplication::keyPressEvent(KeyEvent&) {}
void EmscriptenApplication::keyReleaseEvent(KeyEvent&) {}
void EmscriptenApplication::mousePressEvent(MouseEvent&) {}
void EmscriptenApplication::mouseReleaseEvent(MouseEvent&) {}
void EmscriptenApplication::mouseMoveEvent(MouseMoveEvent&) {}
void EmscriptenApplication::mouseScrollEvent(MouseScrollEvent&) {}
void EmscriptenApplication::textInputEvent(TextInputEvent&) {}

#ifdef MAGNUM_TARGET_GL
EmscriptenApplication::GLConfiguration::GLConfiguration():
    _colorBufferSize{8, 8, 8, 8}, _depthBufferSize{24}, _stencilBufferSize{0},
    _sampleCount{0}
{
    addFlags(Flag::EnableExtensionsByDefault);
}
#endif

int EmscriptenApplication::exec() {
    /* If exit was requested directly in the constructor, exit immediately
       without calling anything else */
    if(_flags & Flag::ExitRequested) return 0;

    redraw();
    return 0;
}

void EmscriptenApplication::redraw() {
    _flags |= Flag::Redraw;

    /* Loop already running, no need to start,
       Note that should javascript runtimes ever be multithreaded, we
       will have a reentrancy issue here. */
    if(_flags & Flag::LoopActive) return;

    /* Start requestAnimationFrame loop */
    _flags |= Flag::LoopActive;
    magnumPlatformRequestAnimationFrame(_callback, this);
}

void EmscriptenApplication::exit(int) {
    _flags |= Flag::ExitRequested;
}

EmscriptenApplication::MouseEvent::Button EmscriptenApplication::MouseEvent::button() const {
    return Button(_event.button);
}

Vector2i EmscriptenApplication::MouseEvent::position() const {
    /* Relies on the target being the canvas, which should be always true for
       mouse events */
    return {Int(_event.targetX), Int(_event.targetY)};
}

EmscriptenApplication::MouseEvent::Modifiers EmscriptenApplication::MouseEvent::modifiers() const {
    Modifiers m;
    if(_event.ctrlKey) m |= Modifier::Ctrl;
    if(_event.shiftKey) m |= Modifier::Shift;
    if(_event.altKey) m |= Modifier::Alt;
    if(_event.metaKey) m |= Modifier::Super;
    return m;
}

EmscriptenApplication::MouseMoveEvent::Buttons EmscriptenApplication::MouseMoveEvent::buttons() const {
    return EmscriptenApplication::MouseMoveEvent::Button(_event.buttons);
}

Vector2i EmscriptenApplication::MouseMoveEvent::position() const {
    /* Relies on the target being the canvas, which should be always true for
       mouse events */
    return {Int(_event.targetX), Int(_event.targetY)};
}

EmscriptenApplication::MouseMoveEvent::Modifiers EmscriptenApplication::MouseMoveEvent::modifiers() const {
    Modifiers m;
    if(_event.ctrlKey) m |= Modifier::Ctrl;
    if(_event.shiftKey) m |= Modifier::Shift;
    if(_event.altKey) m |= Modifier::Alt;
    if(_event.metaKey) m |= Modifier::Super;
    return m;
}

Vector2 EmscriptenApplication::MouseScrollEvent::offset() const {
    /* From emscripten's Browser.getMouseWheelDelta() function in
       library_browser.js:

       DOM_DELTA_PIXEL => 100 pixels = 1 step
       DOM_DELTA_LINE => 3 lines = 1 step
       DOM_DELTA_PAGE => 1 page = 80 steps */
    const Float f = (_event.deltaMode == DOM_DELTA_PIXEL) ? -0.01f :
        ((_event.deltaMode == DOM_DELTA_LINE) ? -1.0f/3.0f : -80.0f);

    return {f*Float(_event.deltaX), f*Float(_event.deltaY)};
}

Vector2i EmscriptenApplication::MouseScrollEvent::position() const {
    /* Relies on the target being the canvas, which should be always true for
       mouse events */
    return {Int(_event.mouse.targetX), Int(_event.mouse.targetY)};
}

EmscriptenApplication::InputEvent::Modifiers EmscriptenApplication::MouseScrollEvent::modifiers() const {
    Modifiers m;
    if(_event.mouse.ctrlKey) m |= Modifier::Ctrl;
    if(_event.mouse.shiftKey) m |= Modifier::Shift;
    if(_event.mouse.altKey) m |= Modifier::Alt;
    if(_event.mouse.metaKey) m |= Modifier::Super;
    return m;
}

Key EmscriptenApplication::KeyEvent::key() const {
    return toKey(_event.key, _event.code);
}

Containers::StringView EmscriptenApplication::KeyEvent::keyName() const {
    if((_event.key[0] >= 'a' && _event.key[0] <= 'z') ||
       (_event.key[0] >= 'A' && _event.key[0] <= 'Z')) return _event.key;

    return _event.code;
}

EmscriptenApplication::InputEvent::Modifiers EmscriptenApplication::KeyEvent::modifiers() const {
    Modifiers m;
    if(_event.ctrlKey) m |= Modifier::Ctrl;
    if(_event.shiftKey) m |= Modifier::Shift;
    if(_event.altKey) m |= Modifier::Alt;
    if(_event.metaKey) m |= Modifier::Super;
    return m;
}

template class BasicScreen<EmscriptenApplication>;
template class BasicScreenedApplication<EmscriptenApplication>;

}}
