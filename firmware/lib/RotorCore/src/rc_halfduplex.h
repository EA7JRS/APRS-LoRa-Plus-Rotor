/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Cola para un bus semidúplex (RS485 de dos hilos) con protocolos pensados para RS232 (GS-232A/B, EasyComm II, Rot2Prog):
// una trama cada vez. Tras una pregunta se espera la respuesta (o replyMs) antes de mandar la siguiente; tras una orden
// sin respuesta, solo lo que tarda en salir. La parada se adelanta a todo y quita lo que no era urgente.
// Sin Arduino: lo prueba test_host.
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace RC {

class HalfDuplexQueue {
public:
    uint32_t baud = 9600;
    uint32_t replyMs = 250;           // tiempo máximo de espera de una respuesta
    uint32_t gapMs = 3;               // vuelta del transceptor tras la última trama
    size_t   maxItems = 6;

    struct Item { std::string data; bool reply; bool urgent; };

    // Encola; devuelve false si la cola está llena (se descarta: las consultas se repiten solas)
    bool push(const std::string& data, bool reply, bool urgent = false) {
        if (urgent) {
            std::vector<Item> keep;
            for (const Item& it : q_) if (it.urgent) keep.push_back(it);
            keep.push_back({data, reply, true});
            q_.swap(keep);
            return true;
        }
        if (q_.size() >= maxItems) return false;
        q_.push_back({data, reply, false});
        return true;
    }

    // Lo siguiente que se puede mandar ahora (vacío si toca esperar)
    bool next(uint32_t now, std::string& out) {
        if (busy(now) || q_.empty()) return false;
        Item it = q_.front();
        q_.erase(q_.begin());
        const uint32_t txMs = (uint32_t)((it.data.size() * 10u * 1000u + baud - 1) / (baud ? baud : 1));
        waiting_ = it.reply;
        until_ = now + txMs + (it.reply ? replyMs : gapMs);
        out = it.data;
        return true;
    }

    // Ha llegado la respuesta completa: el bus queda libre tras la vuelta del transceptor
    void gotReply(uint32_t now) {
        if (!waiting_) return;
        waiting_ = false;
        until_ = now + gapMs;
    }

    bool busy(uint32_t now) const { return (int32_t)(now - until_) < 0; }
    bool idle(uint32_t now) const { return !busy(now) && q_.empty(); }
    bool waitingReply(uint32_t now) const { return waiting_ && busy(now); }
    size_t size() const { return q_.size(); }
    void clear() { q_.clear(); }

private:
    std::vector<Item> q_;
    bool     waiting_ = false;
    uint32_t until_ = 0;
};

}  // namespace RC
