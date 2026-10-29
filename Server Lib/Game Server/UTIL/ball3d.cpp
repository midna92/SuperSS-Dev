// File ball3d.cpp
// Created on 03/10/2020 at 14:31 by Acrisio
// Implementation of the Ball3D class

#if defined(_WIN32)
#pragma pack(1)
#endif
#include "ball3d.hpp"

using namespace stdA;

Ball3D::Ball3D() : m_position(0.f, 0.f, 0.f), m_slope(0.f, 1.f, 0.f), m_velocity(0.f, 0.f, 0.f),
	m_max_height(0.f), m_num_max_height(-1), m_count(0u), m_curve(0.f), m_spin(0.f), m_rotation_curve(0.f),
	m_rotation_spin(0.f), m_acumulation_curve(0.f), m_acumulation_spin(0.f) {
}

Ball3D::~Ball3D() {
}
