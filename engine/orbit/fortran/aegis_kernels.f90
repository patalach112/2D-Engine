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
    pure function normalize_angle(angle) result(out_angle)
        real(c_double), intent(in) :: angle
        real(c_double) :: out_angle

        out_angle = mod(angle, kTwoPi)
        if (out_angle < 0.0_c_double) out_angle = out_angle + kTwoPi
        if (out_angle >= kTwoPi) out_angle = out_angle - kTwoPi
    end function normalize_angle

    function mean_motion(gravitational_parameter, semi_major_axis) result(n)
        real(c_double), intent(in) :: gravitational_parameter, semi_major_axis
        real(c_double) :: n

        n = sqrt(gravitational_parameter / (semi_major_axis * semi_major_axis * semi_major_axis))
    end function mean_motion

    ! Kepler's equation M = E - e sin(E). M is normalized to [0, 2*pi).
    ! Precondition: 0 <= eccentricity < 1.
    function aegis_solve_kepler_f64(mean_anomaly, eccentricity) result(E) &
        bind(c, name='aegis_solve_kepler_f64')
        real(c_double), intent(in), value :: mean_anomaly, eccentricity
        real(c_double) :: E
        real(c_double) :: e, M, f_step, f_prime, step
        integer(c_int) :: iteration

        e = eccentricity
        M = normalize_angle(mean_anomaly)

        ! Cubic initial guess (Battin-style), identical seed to the C++ path.
        E = M + e * sin(M) / (1.0_c_double - sin(M + e) + sin(M) + 1.0e-9_c_double)
        if (.not. (E > -1.0e6_c_double) .or. .not. (E < 1.0e6_c_double) .or. ieee_is_nan(E)) then
            E = M
        end if

        ! Newton-Raphson: E_{n+1} = E_n - (E_n - e sin E_n - M) / (1 - e cos E_n)
        do iteration = 1_c_int, 32_c_int
            f_step = E - e * sin(E) - M
            f_prime = 1.0_c_double - e * cos(E)
            step = f_step / f_prime
            E = E - step
            if (abs(step) < 1.0e-14_c_double * (1.0_c_double + abs(E))) exit
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
        real(c_double) :: a, e, omega, n, M, E, cos_e, sin_e, y_scale
        real(c_double) :: x_orbit, y_orbit, cos_w, sin_w
        real(c_double) :: d_e_dt, vx_orbit, vy_orbit

        a = semi_major_axis
        e = eccentricity
        omega = periapsis_angle

        n = mean_motion(gravitational_parameter, a)
        M = normalize_angle(n * (t - periapsis_time))
        E = aegis_solve_kepler_f64(M, e)

        cos_e = cos(E)
        sin_e = sin(E)
        y_scale = sqrt(1.0_c_double - e * e)

        ! Position in the orbital frame (periapsis along +x'), then rotate by omega.
        x_orbit = a * (cos_e - e)
        y_orbit = a * y_scale * sin_e
        cos_w = cos(omega)
        sin_w = sin(omega)
        out_state(1) = cos_w * x_orbit - sin_w * y_orbit
        out_state(2) = sin_w * x_orbit + cos_w * y_orbit

        ! Velocity: differentiate the E-param form (dE/dt = n / (1 - e cos E)).
        d_e_dt = n / (1.0_c_double - e * cos_e)
        vx_orbit = -a * sin_e * d_e_dt
        vy_orbit = a * y_scale * cos_e * d_e_dt
        out_state(3) = cos_w * vx_orbit - sin_w * vy_orbit
        out_state(4) = sin_w * vx_orbit + cos_w * vy_orbit

        out_state(5) = sqrt(out_state(1) * out_state(1) + out_state(2) * out_state(2))
        out_state(6) = normalize_angle(2.0_c_double * &
            atan2(sqrt(1.0_c_double + e) * sin(E * 0.5_c_double), &
                  sqrt(1.0_c_double - e) * cos(E * 0.5_c_double)))
        out_state(7) = M
        out_state(8) = E
    end subroutine aegis_state_at_f64

    ! Fill out_xy with 2*samples floats (x0, y0, x1, y1, ...), sampled one
    ! full revolution evenly in eccentric anomaly. Precondition: samples >= 3.
    subroutine aegis_sample_path_f32(semi_major_axis, eccentricity, periapsis_angle, &
                                     samples, out_xy) &
        bind(c, name='aegis_sample_path_f32')
        real(c_double), intent(in), value :: semi_major_axis, eccentricity, periapsis_angle
        integer(c_int), intent(in), value :: samples
        real(c_float), intent(out) :: out_xy(*)
        real(c_double) :: a, e, omega, cos_w, sin_w, y_scale
        real(c_double) :: ee, x_orbit, y_orbit, wx, wy
        integer(c_int) :: i

        a = semi_major_axis
        e = eccentricity
        omega = periapsis_angle
        cos_w = cos(omega)
        sin_w = sin(omega)
        y_scale = sqrt(1.0_c_double - e * e)

        do i = 0_c_int, samples - 1_c_int
            ee = kTwoPi * real(i, c_double) / real(samples, c_double)
            x_orbit = a * (cos(ee) - e)
            y_orbit = a * y_scale * sin(ee)
            wx = cos_w * x_orbit - sin_w * y_orbit
            wy = sin_w * x_orbit + cos_w * y_orbit
            out_xy(2_c_int * i + 1_c_int) = real(wx, c_float)
            out_xy(2_c_int * i + 2_c_int) = real(wy, c_float)
        end do
    end subroutine aegis_sample_path_f32

end module aegis_orbit_kernels
