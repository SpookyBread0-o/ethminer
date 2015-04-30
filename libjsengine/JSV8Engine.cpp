//
// Created by Marek Kotewicz on 27/04/15.
//

#include <memory>
#include <libplatform/libplatform.h>
#include "JSV8Engine.h"

using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

class ShellArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
public:
	virtual void* Allocate(size_t length) {
		void* data = AllocateUninitialized(length);
		return data == NULL ? data : memset(data, 0, length);
	}
	virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
	virtual void Free(void* data, size_t) { free(data); }
};

class JSV8Env
{
public:
	JSV8Env();

	~JSV8Env();

private:
	v8::Platform *m_platform;
};

v8::Handle<v8::Context> createShellContext(v8::Isolate* isolate)
{
	v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
	v8::Handle<v8::Context> context = v8::Context::New(isolate, NULL, global);
	if (context.IsEmpty())
	{
		// TODO: throw an exception
	}
	return context;
}

class JSV8Scope
{
public:
	JSV8Scope(v8::Isolate* _isolate):
			m_isolateScope(_isolate),
			m_handleScope(_isolate),
			m_context(createShellContext(_isolate)),
			m_contextScope(m_context)
	{}

	v8::Handle <v8::Context> const& context() const { return m_context; }

private:
	v8::Isolate::Scope m_isolateScope;
	v8::HandleScope m_handleScope;
	v8::Handle <v8::Context> m_context;
	v8::Context::Scope m_contextScope;
};

}
}

JSV8Env JSV8Engine::s_env = JSV8Env();

const char* JSV8Value::asCString() const
{
	if (m_value.IsEmpty())
	{
		// TODO: handle exceptions
		return "";
	}

	else if (m_value->IsUndefined())
		return "undefined";
//	else if (m_value->IsNativeError())
//	{
//		v8::String::Utf8Value str(m_value);
//		return *str ? *str : "error";
//	}
	v8::String::Utf8Value str(m_value);
	return *str ? *str : "<string conversion failed>";
}

JSV8Env::JSV8Env()
{
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;
	v8::V8::InitializeICU();
	m_platform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(m_platform);
	v8::V8::Initialize();
	ShellArrayBufferAllocator array_buffer_allocator;
	v8::V8::SetArrayBufferAllocator(&array_buffer_allocator);
}

JSV8Env::~JSV8Env()
{
	v8::V8::Dispose();
	v8::V8::ShutdownPlatform();
	delete m_platform;
}

JSV8Engine::JSV8Engine():
		m_isolate(v8::Isolate::New()),
		m_scope(new JSV8Scope(m_isolate))
{}

JSV8Engine::~JSV8Engine()
{
	delete m_scope;
	m_isolate->Dispose();
}

JSV8Value JSV8Engine::eval(const char* _cstr) const
{
	v8::HandleScope handleScope(m_isolate);
	v8::TryCatch tryCatch;
	v8::Local<v8::String> source = v8::String::NewFromUtf8(context()->GetIsolate(), _cstr);
	v8::Local<v8::String> name(v8::String::NewFromUtf8(context()->GetIsolate(), "(shell)"));
	v8::ScriptOrigin origin(name);
	v8::Handle<v8::Script> script = v8::Script::Compile(source, &origin);
	// Make sure to wrap the exception in a new handle because
	// the handle returned from the TryCatch is destroyed
	// TODO: improve this cause sometimes incorrect message is being sent!
	if (script.IsEmpty())
		return v8::Exception::Error(v8::Local<v8::String>::New(context()->GetIsolate(), tryCatch.Message()->Get()));

	return JSV8Value(script->Run());
}

v8::Handle<v8::Context> const& JSV8Engine::context() const
{
	return m_scope->context();
}
