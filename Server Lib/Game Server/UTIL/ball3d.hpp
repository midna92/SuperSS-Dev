// File ball3d.hpp
// Created on 03/10/2020 at 14:21 by Acrisio
// Definition of the Ball3D class

#pragma once
#ifndef _STDA_BALL3D_HPP
#define _STDA_BALL3D_HPP

#include "vector3d.hpp"

#include <cstdint>

namespace stdA {

	class Ball3D {

		public:
			Ball3D();
			virtual ~Ball3D();

		public:
			Vector3D m_position;
			Vector3D m_slope;
			Vector3D m_velocity;

			float m_max_height;

			int32_t m_num_max_height;
			uint32_t m_count;

			float m_curve;
			float m_spin;

			float m_rotation_curve;
			float m_rotation_spin;

			float m_acumulation_curve;
			float m_acumulation_spin;

			const float m_mass = 0.045926999f;
			const float m_diametro = 0.14698039f;
	};
}

#endif // !_STDA_BALL3D_HPP
