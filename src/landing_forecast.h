#pragma once

#include "flight.h"
#include "landing_plane.h"

#include <memory>

namespace kosmos
{
    class Universe;
    class Ship;
    struct Base;

    enum class LandingForecastStatus
    {
        invalid_input,
        insufficient_fuel,
        attitude_failure,
        vessel_failure,
        invalid_guidance,
        timeout,
        unavailable_world,
        unavailable_wind,
        captured,
        landed,
        missed_target
    };

    struct LandingForecastResult
    {
        bool captured = false;
        LandingForecastStatus status = LandingForecastStatus::invalid_input;
        sl::dvec3 position{0.0}, velocity{0.0};
        double fuel = 0.0, distance = 0.0, surface_speed = 0.0;
    };

    class LandingForecastStepper
    {
    public:
        LandingForecastStepper(Universe &universe, Ship &ship, const Base &pad, int body_id,
                               bool shuttle, const sl::dvec3 &axis, const sl::dvec3 &direction,
                               double dv, double duration, double thrust, double reserve = 0.0,
                               bool through_touchdown = false);
        ~LandingForecastStepper();
        LandingForecastStepper(const LandingForecastStepper &) = delete;
        LandingForecastStepper &operator=(const LandingForecastStepper &) = delete;
        /** Advance at most max_steps physics steps. Nonpositive budgets do not advance. True when a result is available. */
        bool advance(int max_steps);
        const LandingForecastResult &result() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    /**
     * Advance a private simulation from 40 seconds before a plane burn through local capture.
     * The caller owns the world and vessel; this function mutates both and must never receive the
     * live world. Any terrain referenced by the world must have an independent rotation state.
     */
    LandingForecastResult forecast_landing(Universe &universe, Ship &ship, Base pad, int body_id,
                                           bool shuttle, const sl::dvec3 &axis,
                                           const sl::dvec3 &direction, double dv, double duration, double thrust,
                                           double reserve = 0.0);

    /**
     * Incremental form of forecast_landing_candidate. Terrain sources must remain alive for the
     * lifetime of this object. Worlds with wind callbacks are rejected rather than copying callbacks
     * that can reference mutable live weather. Callers must also account for weather that may start
     * after the snapshot. through_touchdown continues with the shared local approach controller.
     */
    class LandingCandidateForecast
    {
    public:
        LandingCandidateForecast(const Universe &universe, const Ship &ship, const Base &pad, int body_id,
                                 bool shuttle, const sl::dvec3 &axis, const LandingCandidate &candidate,
                                 double thrust, double reserve = 0.0, bool through_touchdown = false);
        ~LandingCandidateForecast();
        LandingCandidateForecast(const LandingCandidateForecast &) = delete;
        LandingCandidateForecast &operator=(const LandingCandidateForecast &) = delete;
        /** Advance at most max_steps physics steps. Nonpositive budgets do not advance. True when a result is available. */
        bool advance(int max_steps);
        const LandingForecastResult &result() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    /**
     * Forecast one candidate from the present flight without advancing it. The coast to alignment
     * uses Kepler rails, suspending nonspherical gravity, and nongravitating vessels are omitted.
     * Additional gravitating vessels and wind callbacks are rejected. A touchdown forecast omits
     * nongravitating vessels and buildings, and cannot certify safety or predict future weather.
     * This bounded calculation may be expensive.
     */
    LandingForecastResult forecast_landing_candidate(const Universe &universe, const Ship &ship, Base pad,
                                                     int body_id, bool shuttle, const sl::dvec3 &axis,
                                                     const LandingCandidate &candidate, double thrust,
                                                     double reserve = 0.0, bool through_touchdown = false);

    struct SoyuzEntryForecastResult
    {
        LandingForecastStatus status = LandingForecastStatus::invalid_input;
        sl::dvec3 relative_position{0.0};
        double time = 0.0, entry_time = 0.0;
    };

    /** Isolated, wind-free Soyuz coast and deorbit to the first safe chute deployment. */
    class SoyuzEntryForecast
    {
    public:
        SoyuzEntryForecast(const Universe &universe, const Ship &ship, double coast = 0.0);
        ~SoyuzEntryForecast();
        SoyuzEntryForecast(const SoyuzEntryForecast &) = delete;
        SoyuzEntryForecast &operator=(const SoyuzEntryForecast &) = delete;
        bool advance(int max_steps);
        const SoyuzEntryForecastResult &result() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
