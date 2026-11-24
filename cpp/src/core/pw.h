// Cliente nativo do PipeWire (libpipewire, pw_thread_loop).
//
// Acompanha pelo registry os nós, os links e o metadado "default"; assina os
// Props dos sinks (volume e mudo chegam por evento, sem consultas). As ações
// (saída padrão, volume, mover streams, parâmetros do filtro, módulos) usam a
// mesma conexão.
//
// Trava: os callbacks rodam na thread do loop com a trava presa. Quem chama de
// fora usa Pw::Lock; a trava é recursiva.
#ifndef QSEQ_PW_H
#define QSEQ_PW_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_metadata;
struct pw_proxy;
struct pw_loop;
struct pw_impl_module;
struct spa_dict;
struct spa_pod;

namespace qseq {

/** Sink de entrada do EQ e o stream de saída dele. */
inline constexpr const char *SINK = "qseq31";
inline constexpr const char *SAIDA = "qseq31.saida";

using Params = std::vector<std::pair<std::string, float>>;

class Pw {
public:
    struct Node;
    struct Link {
        uint32_t out = 0, in = 0;
    };

    Pw();
    ~Pw();
    Pw(const Pw &) = delete;
    Pw &operator=(const Pw &) = delete;

    bool connect(std::string &err);
    /** Ida e volta ao servidor: tudo o que foi pedido antes já chegou. Sem a trava! */
    bool roundtrip();
    bool connected() const;

    void lock();
    void unlock();
    struct Lock {
        Pw &p;
        explicit Lock(Pw &pw) : p(pw) { p.lock(); }
        ~Lock() { p.unlock(); }
    };

    /** Chamado na thread do loop a cada mudança relevante. */
    void setOnChange(std::function<void()> fn);

    // ── Consultas (com a trava) ─────────────────────────────────────────
    std::string defaultSink() const;
    bool hasNode(const std::string &name) const;
    std::optional<uint32_t> nodeId(const std::string &name) const;
    /** Sink para onde o stream `stream` está ligado. */
    std::string linkedSink(const std::string &stream) const;
    /** Streams de programas ligados ao sink (exceto `except`); saídas de filtros ficam de fora. */
    std::vector<uint32_t> streamsInto(const std::string &sink, const std::string &except) const;
    /** Volume no estilo do pactl (raiz cúbica do linear), 0..1.5; nullopt se desconhecido. */
    std::optional<float> volume(const std::string &sink) const;
    bool muted(const std::string &sink) const;

    // ── Ações (com a trava) ─────────────────────────────────────────────
    bool setDefaultSink(const std::string &name);
    bool setVolume(const std::string &sink, float v);
    bool setMute(const std::string &sink, bool on);
    bool moveStream(uint32_t streamId, const std::string &sink);
    bool setParams(const std::string &node, const Params &params);

    pw_impl_module *loadModule(const char *name, const std::string &args, std::string &err);
    void unloadModule(pw_impl_module *mod);
    pw_loop *loop() const;
    pw_core *core() const { return core_; }

    // usados pelos callbacks
    void onGlobal(uint32_t id, const char *type, const struct spa_dict *props);
    void onGlobalRemove(uint32_t id);
    void onDefault(const char *value);
    void onDone(uint32_t id, int seq);
    void onError(uint32_t id, int res, const char *msg);
    void changed();

private:
    const Node *find(const std::string &name) const;
    Node *findMut(const std::string &name);
    bool setProps(Node &n, const struct spa_pod *pod);

    pw_thread_loop *tl_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_registry *registry_ = nullptr;
    pw_metadata *metadata_ = nullptr;
    uint32_t metadataId_ = 0;
    struct Hooks;
    std::unique_ptr<Hooks> hooks_;
    std::map<uint32_t, std::unique_ptr<Node>> nodes_;
    std::map<uint32_t, Link> links_;
    std::string default_;
    int pendingSeq_ = 0;
    bool synced_ = false;
    bool disconnected_ = false;
    std::function<void()> onChange_;
};

} // namespace qseq

#endif
