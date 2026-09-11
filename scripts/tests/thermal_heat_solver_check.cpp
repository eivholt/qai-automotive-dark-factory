// Standalone numerical checks against the same source compiled by Unreal.
// From the repository root under Linux/WSL:
// Compile with g++ -std=c++17 -O2 -Wall -Wextra -pedantic, passing both
// scripts/tests/thermal_heat_solver_check.cpp and
// unreal/Source/DarkFactory/DarkFactoryHeatSolver.cpp, then -o /tmp/thermal-heat-check.
// /tmp/thermal-heat-check
#include "../../unreal/Source/DarkFactory/DarkFactoryHeatSolver.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace DarkFactoryHeat;

    bool Same(const std::vector<Node>& A, const std::vector<Node>& B)
    {
        if (A.size() != B.size()) return false;
        for (std::size_t I = 0; I < A.size(); ++I)
        {
            const bool SameTemperature = A[I].TemperatureC == B[I].TemperatureC ||
                (std::isnan(A[I].TemperatureC) && std::isnan(B[I].TemperatureC));
            const bool SameCapacity = A[I].CapacityJPerK == B[I].CapacityJPerK ||
                (std::isnan(A[I].CapacityJPerK) && std::isnan(B[I].CapacityJPerK));
            if (!SameTemperature || !SameCapacity || A[I].Fixed != B[I].Fixed) return false;
        }
        return true;
    }

    bool Rejects(std::vector<Node> Nodes, const std::vector<Link>& Links,
        double Seconds = 1, double MaxStep = 1)
    {
        const auto Before = Nodes;
        const Result R = Advance(Nodes, Links, Seconds, MaxStep);
        return !R.Valid && R.SuppliedEnergyJ == 0 && R.EnergyResidualJ == 0 && Same(Nodes, Before);
    }
}

int main()
{
    using namespace DarkFactoryHeat;
    std::vector<std::pair<std::string, bool>> Checks;
    Checks.emplace_back("built_in_self_test", SelfTest());

    // Analytic solution of two finite capacities connected by a constant G.
    const std::vector<Node> Initial = {{95, 100, false}, {24, 300, false}};
    const std::vector<Link> Conductive = {{0, 1, 2, 0}};
    const double Mean = (100.0 * 95 + 300.0 * 24) / 400;
    const double Difference = 71 * std::exp(-2.0 * (1.0 / 100 + 1.0 / 300) * 30);
    const double ExactA = Mean + .75 * Difference;
    const double ExactB = Mean - .25 * Difference;
    auto Coarse = Initial;
    auto Fine = Initial;
    const Result CoarseResult = Advance(Coarse, Conductive, 30, 1);
    const Result FineResult = Advance(Fine, Conductive, 30, .01);
    const double CoarseError = std::max(std::abs(Coarse[0].TemperatureC - ExactA),
        std::abs(Coarse[1].TemperatureC - ExactB));
    const double FineError = std::max(std::abs(Fine[0].TemperatureC - ExactA),
        std::abs(Fine[1].TemperatureC - ExactB));
    Checks.emplace_back("unequal_capacity_analytic_convergence", CoarseResult.Valid &&
        FineResult.Valid && FineError < .003 && FineError < CoarseError * .02);

    // Multiple simultaneous edges exercise the sum-G step bound. An in-place
    // edge temperature update would break symmetry and this energy balance.
    std::vector<Node> Star = {{95, 1, false}, {24, 2, false}, {24, 2, false}, {24, 2, false}};
    const std::vector<Link> StarLinks = {{0, 1, 100, .01}, {0, 2, 100, .01}, {0, 3, 100, .01}};
    const Result StarResult = Advance(Star, StarLinks, .25, 1);
    double Lowest = 95, Highest = 24;
    for (const Node& N : Star)
    {
        Lowest = std::min(Lowest, N.TemperatureC);
        Highest = std::max(Highest, N.TemperatureC);
    }
    Checks.emplace_back("adaptive_jacobi_star", StarResult.Valid && StarResult.Steps > 1 &&
        Lowest >= 24 && Highest <= 95 && std::abs(StarResult.EnergyResidualJ) < 1e-9 &&
        Star[1].TemperatureC == Star[2].TemperatureC && Star[2].TemperatureC == Star[3].TemperatureC);

    auto StarRepeat = std::vector<Node>{{95, 1, false}, {24, 2, false}, {24, 2, false}, {24, 2, false}};
    const Result RepeatResult = Advance(StarRepeat, StarLinks, .25, 1);
    Checks.emplace_back("deterministic_replay", RepeatResult.Valid && Same(Star, StarRepeat) &&
        RepeatResult.Steps == StarResult.Steps && RepeatResult.EnergyResidualJ == StarResult.EnergyResidualJ);

    // Radiation is integrated over all wavelengths. This reference uses direct
    // fourth powers, independently of the solver's cancellation-safe factorization.
    std::vector<Node> Radiation = {{95, 1, true}, {24, 100, false}};
    const Result RadiationResult = Advance(Radiation, {{0, 1, 0, .01}}, .1, .1);
    const double RadiationWatts = 5.670374419e-8 * .01 *
        (std::pow(368.15, 4) - std::pow(297.15, 4));
    Checks.emplace_back("stefan_boltzmann_reference", RadiationResult.Valid &&
        RadiationResult.Steps == 1 && std::abs(RadiationResult.SuppliedEnergyJ - .1 * RadiationWatts) < 1e-12 &&
        std::abs(RadiationResult.EnergyResidualJ) < 1e-9);

    std::vector<Node> ClosedRadiation = {{95, 40, false}, {24, 120, false}};
    const Result ClosedRadiationResult = Advance(ClosedRadiation, {{0, 1, 0, .1}}, 90, .1);
    Checks.emplace_back("isolated_radiation_conservation", ClosedRadiationResult.Valid &&
        ClosedRadiationResult.SuppliedEnergyJ == 0 && std::abs(ClosedRadiationResult.EnergyResidualJ) < 1e-8 &&
        ClosedRadiation[0].TemperatureC > ClosedRadiation[1].TemperatureC &&
        ClosedRadiation[0].TemperatureC < 95 && ClosedRadiation[1].TemperatureC > 24);

    std::vector<Node> Sink = {{24, 1, true}, {95, 100, false}};
    const Result SinkResult = Advance(Sink, {{0, 1, 2, 0}}, 10, .1);
    Checks.emplace_back("fixed_reservoir_sink_sign", SinkResult.Valid && Sink[0].TemperatureC == 24 &&
        SinkResult.SuppliedEnergyJ < 0 && std::abs(SinkResult.EnergyResidualJ) < 1e-8);

    auto ZeroArea = Initial;
    const Result ZeroResult = Advance(ZeroArea, {{0, 1, 0, 0}}, 1e9);
    Checks.emplace_back("zero_emissivity_or_view_factor", ZeroResult.Valid && ZeroResult.Steps == 0 &&
        ZeroResult.SuppliedEnergyJ == 0 && Same(Initial, ZeroArea));

    auto ZeroTime = Initial;
    const Result ZeroTimeResult = Advance(ZeroTime, Conductive, 0);
    std::vector<Node> Empty;
    const Result EmptyResult = Advance(Empty, {}, 2);
    Checks.emplace_back("zero_time_and_empty_network", ZeroTimeResult.Valid && ZeroTimeResult.Steps == 0 &&
        Same(Initial, ZeroTime) && EmptyResult.Valid && EmptyResult.Steps == 0);

    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    bool InvalidRejected = true;
    for (const double BadTemperature : {NaN, Infinity, -Infinity, -273.15, -300.0})
        InvalidRejected = Rejects({{BadTemperature, 1, false}, {24, 1, false}}, Conductive) && InvalidRejected;
    for (const double BadCapacity : {NaN, Infinity, 0.0, -1.0})
        InvalidRejected = Rejects({{95, BadCapacity, false}, {24, 1, false}}, Conductive) && InvalidRejected;
    for (const double BadTime : {NaN, Infinity, -1.0})
        InvalidRejected = Rejects(Initial, Conductive, BadTime) && InvalidRejected;
    for (const double BadStep : {NaN, Infinity, 0.0, -1.0})
        InvalidRejected = Rejects(Initial, Conductive, 1, BadStep) && InvalidRejected;
    for (const double BadCoefficient : {NaN, Infinity, -1.0})
    {
        InvalidRejected = Rejects(Initial, {{0, 1, BadCoefficient, 0}}) && InvalidRejected;
        InvalidRejected = Rejects(Initial, {{0, 1, 0, BadCoefficient}}) && InvalidRejected;
    }
    InvalidRejected = Rejects(Initial, {{-1, 1, 1, 0}}) && InvalidRejected;
    InvalidRejected = Rejects(Initial, {{0, 2, 1, 0}}) && InvalidRejected;
    InvalidRejected = Rejects({}, {{0, 0, 0, 0}}) && InvalidRejected;
    Checks.emplace_back("invalid_input_transactional_rejection", InvalidRejected);

    auto StepLimited = Initial;
    const Result LimitResult = Advance(StepLimited, Conductive, 200001, 1);
    Checks.emplace_back("step_limit_transactional_rejection", !LimitResult.Valid && LimitResult.Steps == 200000 &&
        LimitResult.SuppliedEnergyJ == 0 && LimitResult.EnergyResidualJ == 0 && Same(StepLimited, Initial));
    Checks.emplace_back("arithmetic_overflow_rejection", Rejects(Initial,
        {{0, 1, 1e308, 0}, {0, 1, 1e308, 0}}) &&
        Rejects({{1e200, 1, false}, {24, 1, false}}, {{0, 1, 0, .1}}));

    bool Passed = true;
    for (const auto& Check : Checks) Passed = Passed && Check.second;
    std::cout << std::setprecision(17) << "{\n  \"valid\": " << (Passed ? "true" : "false") << ",\n  \"checks\": {\n";
    for (std::size_t I = 0; I < Checks.size(); ++I)
        std::cout << "    \"" << Checks[I].first << "\": " << (Checks[I].second ? "true" : "false")
            << (I + 1 == Checks.size() ? "\n" : ",\n");
    std::cout << "  },\n  \"metrics\": {\n"
        << "    \"analytic_coarse_error_c\": " << CoarseError << ",\n"
        << "    \"analytic_fine_error_c\": " << FineError << ",\n"
        << "    \"star_steps\": " << StarResult.Steps << ",\n"
        << "    \"star_energy_residual_j\": " << StarResult.EnergyResidualJ << ",\n"
        << "    \"radiation_reference_w\": " << RadiationWatts << ",\n"
        << "    \"isolated_radiation_energy_residual_j\": " << ClosedRadiationResult.EnergyResidualJ << ",\n"
        << "    \"fixed_sink_supplied_energy_j\": " << SinkResult.SuppliedEnergyJ << "\n  }\n}\n";
    return Passed ? 0 : 1;
}
