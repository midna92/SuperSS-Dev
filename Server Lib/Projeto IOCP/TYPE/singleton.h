// File singleton.h
// Created on 27/04/2019 at 11:11 by Acrisio
// Definition and implementation of the template class singleton

#pragma once
#ifndef _STDA_SINGLETON_H
#define _STDA_SINGLETON_H

namespace stdA {

    template<class _ST> class Singleton {
        public:
            static _ST& getInstance();

            // delete copy and move constructors and assign operators
            Singleton(Singleton const& ) = delete;              // Copy constructor
            Singleton(Singleton&& ) = delete;                   // Move constructor
            Singleton& operator=(Singleton const& ) = delete;   // Copy assign
            Singleton& operator=(Singleton&& ) = delete;        // Move assign

        protected:
            Singleton() {};     // Constructor
            ~Singleton() {};    // Destructor
    };

    template<class _ST> _ST& Singleton<_ST>::getInstance()  {

        // Only thread-safe from C++ 11 onwards (more recent)
        static _ST myInstance;

        return myInstance;
    }
}

#endif // !_STDA_SINGLETON_H