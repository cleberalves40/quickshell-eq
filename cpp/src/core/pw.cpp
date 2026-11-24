#include "pw.h"

#include "json.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <set>

#include <pipewire/extensions/metadata.h>
#include <pipewire/impl-module.h>
#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>

namespace qseq {

constexpr uint32_t MAX_CH = 64;

struct Pw::Node {
    Pw *owner = nullptr;
    uint32_t id = 0;
    uint64_t serial = 0;
    std::string name;
    std::string cls;
    pw_proxy *proxy = nullptr;
    spa_hook objListener{};
    spa_hook proxyListener{};
    std::vector<float> vols;
    bool mute = false;
    bool haveVol = false;
};

struct Pw::Hooks {
    spa_hook core{};
    spa_hook registry{};
    spa_hook metadata{};
};

namespace {

// ── callbacks em C ──────────────────────────────────────────────────────

void coreDone(void *data, uint32_t id, int seq) { static_cast<Pw *>(data)->onDone(id, seq); }
void coreError(void *data, uint32_t id, int, int res, const char *msg)
{
    static_cast<Pw *>(data)->onError(id, res, msg);
}
const pw_core_events coreEvents = [] {
    pw_core_events e{};
    e.version = PW_VERSION_CORE_EVENTS;
    e.done = coreDone;
    e.error = coreError;
    return e;
}();

void regGlobal(void *data, uint32_t id, uint32_t, const char *type, uint32_t, const spa_dict *props)
{
    static_cast<Pw *>(data)->onGlobal(id, type, props);
}
void regRemove(void *data, uint32_t id) { static_cast<Pw *>(data)->onGlobalRemove(id); }
const pw_registry_events registryEvents = [] {
    pw_registry_events e{};
    e.version = PW_VERSION_REGISTRY_EVENTS;
    e.global = regGlobal;
    e.global_remove = regRemove;
    return e;
}();

int mdProperty(void *data, uint32_t subject, const char *key, const char *, const char *value)
{
    if (subject == PW_ID_CORE && key && std::strcmp(key, "default.audio.sink") == 0)
        static_cast<Pw *>(data)->onDefault(value);
    return 0;
}
const pw_metadata_events metadataEvents = [] {
    pw_metadata_events e{};
    e.version = PW_VERSION_METADATA_EVENTS;
    e.property = mdProperty;
    return e;
}();

void nodeParam(void *data, int, uint32_t id, uint32_t, uint32_t, const spa_pod *param)
{
    auto *n = static_cast<Pw::Node *>(data);
    if (id != SPA_PARAM_Props || !param || !spa_pod_is_object_type(param, SPA_TYPE_OBJECT_Props))
        return;
    auto *obj = reinterpret_cast<const spa_pod_object *>(param);
    const spa_pod_prop *prop;
    bool any = false;
    SPA_POD_OBJECT_FOREACH(obj, prop)
    {
        if (prop->key == SPA_PROP_channelVolumes) {
            float v[MAX_CH];
            uint32_t c = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, v, MAX_CH);
            if (c > 0) {
                n->vols.assign(v, v + c);
                n->haveVol = true;
                any = true;
            }
        } else if (prop->key == SPA_PROP_mute) {
            bool m;
            if (spa_pod_get_bool(&prop->value, &m) == 0) {
                n->mute = m;
                any = true;
            }
        }
    }
    if (any)
        n->owner->changed();
}
const pw_node_events nodeEvents = [] {
    pw_node_events e{};
    e.version = PW_VERSION_NODE_EVENTS;
    e.param = nodeParam;
    return e;
}();

uint32_t toU32(const char *s)
{
    return s ? static_cast<uint32_t>(std::strtoul(s, nullptr, 10)) : 0;
}

void dropNode(Pw::Node &n)
{
    if (!n.proxy)
        return;
    spa_hook_remove(&n.objListener);
    pw_proxy_destroy(n.proxy);
    n.proxy = nullptr;
}

} // namespace

// ── conexão ─────────────────────────────────────────────────────────────

Pw::Pw() : hooks_(std::make_unique<Hooks>()) {}

Pw::~Pw()
{
    if (!tl_)
        return;
    pw_thread_loop_stop(tl_);
    for (auto &[id, n] : nodes_)
        dropNode(*n);
    nodes_.clear();
    if (metadata_) {
        spa_hook_remove(&hooks_->metadata);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(metadata_));
    }
    if (registry_) {
        spa_hook_remove(&hooks_->registry);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry_));
    }
    if (core_) {
        spa_hook_remove(&hooks_->core);
        pw_core_disconnect(core_);
    }
    if (context_)
        pw_context_destroy(context_);
    pw_thread_loop_destroy(tl_);
}

bool Pw::connect(std::string &err)
{
    tl_ = pw_thread_loop_new("qseq", nullptr);
    if (!tl_) {
        err = "pw_thread_loop_new falhou";
        return false;
    }
    // O module-rt não funciona no FreeBSD (rebaixa a thread de dados); o tempo
    // real vem do rtprio(1) do processo.
    pw_properties *props = pw_properties_new("module.rt", "false", PW_KEY_APP_NAME, "eq-tui", nullptr);
    context_ = pw_context_new(pw_thread_loop_get_loop(tl_), props, 0);
    if (!context_) {
        err = std::string("pw_context_new: ") + std::strerror(errno);
        return false;
    }
    if (pw_thread_loop_start(tl_) < 0) {
        err = "pw_thread_loop_start falhou";
        return false;
    }
    {
        Lock l(*this);
        core_ = pw_context_connect(context_, nullptr, 0);
        if (!core_) {
            err = std::string("sem conexão com o PipeWire: ") + std::strerror(errno);
            return false;
        }
        pw_core_add_listener(core_, &hooks_->core, &coreEvents, this);
        registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener(registry_, &hooks_->registry, &registryEvents, this);
    }
    // 1ª volta: os globais; 2ª: os Props dos sinks e o metadado.
    if (!roundtrip() || !roundtrip()) {
        err = "o PipeWire não respondeu";
        return false;
    }
    return true;
}

bool Pw::roundtrip()
{
    Lock l(*this);
    if (disconnected_ || !core_)
        return false;
    synced_ = false;
    pendingSeq_ = pw_core_sync(core_, PW_ID_CORE, 0);
    while (!synced_ && !disconnected_) {
        if (pw_thread_loop_timed_wait(tl_, 5) != 0)
            return false;
    }
    return !disconnected_;
}

bool Pw::connected() const { return core_ && !disconnected_; }
void Pw::lock() { pw_thread_loop_lock(tl_); }
void Pw::unlock() { pw_thread_loop_unlock(tl_); }

void Pw::setOnChange(std::function<void()> fn)
{
    Lock l(*this);
    onChange_ = std::move(fn);
}

void Pw::changed()
{
    if (onChange_)
        onChange_();
}

void Pw::onDone(uint32_t id, int seq)
{
    if (id == PW_ID_CORE && seq == pendingSeq_) {
        synced_ = true;
        pw_thread_loop_signal(tl_, false);
    }
}

void Pw::onError(uint32_t id, int res, const char *)
{
    if (id == PW_ID_CORE && res == -EPIPE) {
        disconnected_ = true;
        pw_thread_loop_signal(tl_, false);
        changed();
    }
}

void Pw::onGlobal(uint32_t id, const char *type, const spa_dict *props)
{
    if (!type || !props)
        return;
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        auto n = std::make_unique<Node>();
        n->owner = this;
        n->id = id;
        const char *s;
        if ((s = spa_dict_lookup(props, PW_KEY_NODE_NAME)))
            n->name = s;
        if ((s = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS)))
            n->cls = s;
        if ((s = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL)))
            n->serial = std::strtoull(s, nullptr, 10);
        if (n->cls == "Audio/Sink") {
            n->proxy = static_cast<pw_proxy *>(
                pw_registry_bind(registry_, id, type, PW_VERSION_NODE, 0));
            if (n->proxy) {
                auto *node = reinterpret_cast<pw_node *>(n->proxy);
                pw_node_add_listener(node, &n->objListener, &nodeEvents, n.get());
                uint32_t ids[] = {SPA_PARAM_Props};
                pw_node_subscribe_params(node, ids, 1);
            }
        }
        nodes_[id] = std::move(n);
        changed();
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        links_[id] = Link{toU32(spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_NODE)),
                          toU32(spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE))};
        changed();
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && !metadata_) {
        const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (name && std::strcmp(name, "default") == 0) {
            metadata_ = static_cast<pw_metadata *>(
                pw_registry_bind(registry_, id, type, PW_VERSION_METADATA, 0));
            if (metadata_) {
                metadataId_ = id;
                pw_metadata_add_listener(metadata_, &hooks_->metadata, &metadataEvents, this);
            }
        }
    }
}

void Pw::onGlobalRemove(uint32_t id)
{
    if (auto it = nodes_.find(id); it != nodes_.end()) {
        dropNode(*it->second);
        nodes_.erase(it);
        changed();
    } else if (links_.erase(id)) {
        changed();
    } else if (metadata_ && id == metadataId_) {
        spa_hook_remove(&hooks_->metadata);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(metadata_));
        metadata_ = nullptr;
        default_.clear();
        changed();
    }
}

void Pw::onDefault(const char *value)
{
    default_.clear();
    json::Value v;
    if (value && json::parse(value, v))
        if (auto n = v.get("name"); n && n->type == json::Value::String)
            default_ = n->str;
    changed();
}

// ── consultas ───────────────────────────────────────────────────────────

const Pw::Node *Pw::find(const std::string &name) const
{
    for (const auto &[id, n] : nodes_)
        if (n->name == name)
            return n.get();
    return nullptr;
}

Pw::Node *Pw::findMut(const std::string &name)
{
    return const_cast<Node *>(find(name));
}

std::string Pw::defaultSink() const { return default_; }
bool Pw::hasNode(const std::string &name) const { return find(name) != nullptr; }

std::optional<uint32_t> Pw::nodeId(const std::string &name) const
{
    if (auto n = find(name))
        return n->id;
    return std::nullopt;
}

std::string Pw::linkedSink(const std::string &stream) const
{
    auto sid = nodeId(stream);
    if (!sid)
        return {};
    for (const auto &[id, l] : links_) {
        if (l.out != *sid)
            continue;
        if (auto it = nodes_.find(l.in); it != nodes_.end())
            return it->second->name;
    }
    return {};
}

std::vector<uint32_t> Pw::streamsInto(const std::string &sink, const std::string &except) const
{
    std::set<uint32_t> out;
    auto sid = nodeId(sink);
    if (!sid)
        return {};
    for (const auto &[id, l] : links_) {
        if (l.in != *sid)
            continue;
        auto it = nodes_.find(l.out);
        if (it != nodes_.end() && it->second->cls == "Stream/Output/Audio" && it->second->name != except)
            out.insert(l.out);
    }
    return {out.begin(), out.end()};
}

std::optional<float> Pw::volume(const std::string &sink) const
{
    auto n = find(sink);
    if (!n || !n->haveVol || n->vols.empty())
        return std::nullopt;
    float sum = 0;
    for (float v : n->vols)
        sum += std::cbrt(v);
    return sum / static_cast<float>(n->vols.size());
}

bool Pw::muted(const std::string &sink) const
{
    auto n = find(sink);
    return n && n->mute;
}

// ── ações ───────────────────────────────────────────────────────────────

bool Pw::setDefaultSink(const std::string &name)
{
    if (!metadata_)
        return false;
    std::string v = "{ \"name\": " + json::quote(name) + " }";
    pw_metadata_set_property(metadata_, PW_ID_CORE, "default.configured.audio.sink",
                             "Spa:String:JSON", v.c_str());
    return true;
}

bool Pw::setProps(Node &n, const spa_pod *pod)
{
    if (!n.proxy)
        return false;
    pw_node_set_param(reinterpret_cast<pw_node *>(n.proxy), SPA_PARAM_Props, 0, pod);
    return true;
}

bool Pw::setVolume(const std::string &sink, float v)
{
    Node *n = findMut(sink);
    if (!n)
        return false;
    float lin = std::pow(std::clamp(v, 0.0f, 1.5f), 3.0f);
    uint32_t ch = n->vols.empty() ? 2 : static_cast<uint32_t>(n->vols.size());
    std::vector<float> vols(ch, lin);
    uint8_t buf[1024];
    spa_pod_builder b;
    spa_pod_builder_init(&b, buf, sizeof buf);
    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
    spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, ch, vols.data());
    auto *pod = static_cast<spa_pod *>(spa_pod_builder_pop(&b, &f));
    if (!setProps(*n, pod))
        return false;
    n->vols = vols; // o evento confirma depois; a leitura já reflete a mudança
    n->haveVol = true;
    return true;
}

bool Pw::setMute(const std::string &sink, bool on)
{
    Node *n = findMut(sink);
    if (!n)
        return false;
    uint8_t buf[256];
    spa_pod_builder b;
    spa_pod_builder_init(&b, buf, sizeof buf);
    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
    spa_pod_builder_bool(&b, on);
    auto *pod = static_cast<spa_pod *>(spa_pod_builder_pop(&b, &f));
    if (!setProps(*n, pod))
        return false;
    n->mute = on;
    return true;
}

bool Pw::moveStream(uint32_t streamId, const std::string &sink)
{
    const Node *n = find(sink);
    if (!metadata_ || !n)
        return false;
    // O mesmo que o pipewire-pulse faz no move-sink-input.
    pw_metadata_set_property(metadata_, streamId, "target.node", "Spa:Id",
                             std::to_string(n->id).c_str());
    pw_metadata_set_property(metadata_, streamId, "target.object", "Spa:Id",
                             std::to_string(n->serial).c_str());
    return true;
}

bool Pw::setParams(const std::string &node, const Params &params)
{
    Node *n = findMut(node);
    if (!n)
        return false;
    std::vector<uint8_t> buf(64 + params.size() * 96);
    spa_pod_builder b;
    spa_pod_builder_init(&b, buf.data(), static_cast<uint32_t>(buf.size()));
    spa_pod_frame f[2];
    spa_pod_builder_push_object(&b, &f[0], SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_params, 0);
    spa_pod_builder_push_struct(&b, &f[1]);
    for (const auto &[k, v] : params) {
        spa_pod_builder_string(&b, k.c_str());
        spa_pod_builder_float(&b, v);
    }
    spa_pod_builder_pop(&b, &f[1]);
    auto *pod = static_cast<spa_pod *>(spa_pod_builder_pop(&b, &f[0]));
    return pod && setProps(*n, pod);
}

pw_impl_module *Pw::loadModule(const char *name, const std::string &args, std::string &err)
{
    pw_impl_module *m = pw_context_load_module(context_, name, args.c_str(), nullptr);
    if (!m)
        err = std::string(name) + ": " + std::strerror(errno);
    return m;
}

void Pw::unloadModule(pw_impl_module *mod)
{
    if (mod)
        pw_impl_module_destroy(mod);
}

pw_loop *Pw::loop() const { return pw_thread_loop_get_loop(tl_); }

} // namespace qseq
