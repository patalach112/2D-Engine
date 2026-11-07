! Aegis2D -- Fortran orbital-mechanics kernels (Milestone 6).
!
! Two-body Keplerian propagation, exposed to C/C++ through iso_c_binding so
! the engine can call the kernels directly (no Fortran runtime objects cross
! the boundary, no allocations, no globals, no I/O).
!
! Numerical contract: identical algorithm to the C++ fallback in
! engine/orbit/src/orbit_fallback.cpp (Newton-Raphson on Kepler's equation
! with a cubic seed, angular range reduction via Fortran mod()). The two
! backends are A/B unit-tested to agree at ~1e-12.
module aegis_orbit_kernels
    use, intrinsic :: iso_c_binding, only: c_double, c_float, c_int
    use, intrinsic :: ieee_arithmetic, only: ieee_is_nan
    implicit none
    private
    public :: aegis_solve_kepler_f64, aegis_state_at_f64, aegis_sample_path_f32

    real(c_double), parameter :: kPi = 3.14159265358979323846264338327950_c_double
    real(c_double), parameter :: kTwoPi = 2.0_c_double * kPi

contains

    ! Bring an angle into [0, 2*pi). Mirrors normalize_angle() in the C++
    ! fallback, including the fmod/mod rounding guard.
    pure function normalize_angle(angle) result(angle_out)
        real(c_double), intent(in) :: angle
        real(c_double) :: angle_out

        angle_out = mod(angle, kTwoPi)
        if (angle_out < 0.0_c_double) angle_out = angle_out + kTwoPi
        if (angle_out >= kTwoPi) angle_out = angle_out - kTwoPi
    end function normalize_angle

    function mean_motion(gravitational_parameter, semi_major_axis) result(motion_val)
        real(c_double), intent(in) :: gravitational_parameter, semi_major_axis
        real(c_double) :: motion_val

        motion_val = sqrt(gravitational_parameter / (semi_major_axis * semi_major_axis * semi_major_axis))
    end function mean_motion

    ! Kepler's equation M = E - ecc sin(E). M is normalized to [0, 2*pi).
    ! Precondition: 0 <= eccentricity < 1.
    function aegis_solve_kepler_f64(mean_anomaly, eccentricity) result(ecc_sol) &
        bind(c, name='aegis_solve_kepler_f64')
        real(c_double), intent(in), value :: mean_anomaly, eccentricity
        real(c_double) :: ecc_sol
        real(c_double) :: ecc, mean_anom, f_val, f_prime_val, nr_step
        integer(c_int) :: iteration

        ecc = eccentricity
        mean_anom = normalize_angle(mean_anomaly)

        ! Cubic initial guess (Battin-style), identical seed to the C++ path.
        ecc_sol = mean_anom + ecc * sin(mean_anom) / (1.0_c_double - sin(mean_anom + ecc) + sin(mean_anom) + 1.0e-9_c_double)
        if (.not. (ecc_sol > -1.0e6_c_double) .or. .not. (ecc_sol < 1.0e6_c_double) .or. ieee_is_nan(ecc_sol)) then
            ecc_sol = mean_anom
        end if

        ! Newton-Raphson: E_{n+1} = E_n - (E_n - ecc sin E_n - M) / (1 - ecc cos E_n)
        do iteration = 1_c_int, 32_c_int
            f_val = ecc_sol - ecc * sin(ecc_sol) - mean_anom
            f_prime_val = 1.0_c_double - ecc * cos(ecc_sol)
            nr_step = f_val / f_prime_val
            ecc_sol = ecc_sol - nr_step
            if (abs(nr_step) < 1.0e-14_c_double * (1.0_c_double + abs(ecc_sol))) exit
        end do
    end function aegis_solve_kepler_f64

    ! Propagate the orbit to absolute time t (seconds).
    ! out_state layout:
    !   1 x        2 y        3 vx       4 vy
    !   5 radius   6 true anomaly   7 mean anomaly   8 eccentric anomaly
    subroutine aegis_state_at_f64(semi_major_axis, eccentricity, periapsis_angle, &
                                  gravitational_parameter, periapsis_time, t, &
                                  out_state) &
        bind(c, name='aegis_state_at_f64')
        real(c_double), intent(in), value :: semi_major_axis, eccentricity, periapsis_angle
        real(c_double), intent(in), value :: gravitational_parameter, periapsis_time, t
        real(c_double), intent(out) :: out_state(8)
        real(c_double) :: axis_a, ecc, omega_w, motion_n, mean_anom, ecc_anom
        real(c_double) :: cos_ecc, sin_ecc, y_scale_v
        real(c_double) :: x_orb, y_orb, cos_w, sin_w
        real(c_double) :: d_ecc_dt, vx_orb, vy_orb

        axis_a = semi_major_axis
        ecc = eccentricity
        omega_w = periapsis_angle

        motion_n = mean_motion(gravitational_parameter, axis_a)
        mean_anom = normalize_angle(motion_n * (t - periapsis_time))
        ecc_anom = aegis_solve_kepler_f64(mean_anom, ecc)

        cos_ecc = cos(ecc_anom)
        sin_ecc = sin(ecc_anom)
        y_scale_v = sqrt(1.0_c_double - ecc * ecc)

        ! Position in the orbital frame (periapsis along +x'), then rotate by omega.
        x_orb = axis_a * (cos_ecc - ecc)
        y_orb = axis_a * y_scale_v * sin_ecc
        cos_w = cos(omega_w)
        sin_w = sin(omega_w)
        out_state(1) = cos_w * x_orb - sin_w * y_orb
        out_state(2) = sin_w * x_orb + cos_w * y_orb

        ! Velocity: differentiate the E-param form (dE/dt = n / (1 - ecc cos E)).
        d_ecc_dt = motion_n / (1.0_c_double - ecc * cos_ecc)
        vx_orb = -axis_a * sin_ecc * d_ecc_dt
        vy_orb = axis_a * y_scale_v * cos_ecc * d_ecc_dt
        out_state(3) = cos_w * vx_orb - sin_w * vy_orb
        out_state(4) = sin_w * vx_orb + cos_w * vy_orb

        out_state(5) = sqrt(out_state(1) * out_state(1) + out_state(2) * out_state(2))
        out_state(6) = normalize_angle(2.0_c_double * &
            atan2(sqrt(1.0_c_double + ecc) * sin(ecc_anom * 0.5_c_double), &
                  sqrt(1.0_c_double - ecc) * cos(ecc_anom * 0.5_c_double)))
        out_state(7) = mean_anom
        out_state(8) = ecc_anom
    end subroutine aegis_state_at_f64

    ! Fill out_xy with 2*samples floats (x0, y0, x1, y1, ...), sampled one
    ! full revolution evenly in eccentric anomaly. Precondition: samples >= 3.
    subroutine aegis_sample_path_f32(semi_major_axis, eccentricity, periapsis_angle, &
                                     samples, out_xy) &
        bind(c, name='aegis_sample_path_f32')
        real(c_double), intent(in), value :: semi_major_axis, eccentricity, periapsis_angle
        integer(c_int), intent(in), value :: samples
        real(c_float), intent(out) :: out_xy(*)
        real(c_double) :: axis_a, ecc, omega_w, cos_w, sin_w, y_scale_v
        real(c_double) :: ecc_anom, x_orb, y_orb, world_x, world_y
        integer(c_int) :: i_idx

        axis_a = semi_major_axis
        ecc = eccentricity
        omega_w = periapsis_angle
        cos_w = cos(omega_w)
        sin_w = sin(omega_w)
        y_scale_v = sqrt(1.0_c_double - ecc * ecc)

        do i_idx = 0_c_int, samples - 1_c_int
            ecc_anom = kTwoPi * real(i_idx, c_double) / real(samples, c_double)
            x_orb = axis_a * (cos(ecc_anom) - ecc)
            y_orb = axis_a * y_scale_v * sin(ecc_anom)
            world_x = cos_w * x_orb - sin_w * y_orb
            world_y = sin_w * x_orb + cos_w * y_orb
            out_xy(2_c_int * i_idx + 1_c_int) = real(world_x, c_float)
            out_xy(2_c_int * i_idx + 2_c_int) = real(world_y, c_float)
        end do
    end subroutine aegis_sample_path_f32

end module aegis_orbit_kernels
