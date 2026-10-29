// File job.h
// Created on 21/05/2017 by Acrisio
// Definition of class job

#pragma once
#ifndef _STDA_JOB_H
#define _STDA_JOB_H

#include "../TYPE/stdAType.h"

namespace stdA {
    class job {
        public:
            job(call_func _func, void* _arg1, void* _arg2);
            ~job();

			bool isValid();

			int execute_job();

			job *newInstanceJob();

        private:
            call_func job_func;
			void* job_arg1;
			void* job_arg2;
    };
}

#endif