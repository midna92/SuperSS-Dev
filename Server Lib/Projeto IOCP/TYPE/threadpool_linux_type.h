// File threadpool_linux_type.h
// Created on 11/06/2021 at 10:39 by Acrisio
// Definition of the types used in the Linux threadpool port

#pragma once
#ifndef _STDA_THREADPOOL_LINUX_TYPE_H
#define _STDA_THREADPOOL_LINUX_TYPE_H

#include "../SOCKET/session.h"

namespace stdA {

    struct stThreadpoolMessage {
    public:
        stThreadpoolMessage(uint32_t _ul = 0u) : _session(nullptr), buffer(nullptr), dwIOsize(0u) {};
        stThreadpoolMessage(session* __session, Buffer *_buffer, DWORD _dwIOsize) : _session(__session), buffer(_buffer), dwIOsize(_dwIOsize) {};

    public:
        session* _session;
        Buffer *buffer;
        DWORD dwIOsize;
    };
}

#endif // !_STDA_THREADPOOL_LINUX_TYPE_H