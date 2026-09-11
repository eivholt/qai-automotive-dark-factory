#include "DarkFactoryHeatSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DarkFactoryHeat
{
    namespace
    {
        constexpr double Sigma = 5.670374419e-8; // W / (m^2 K^4), CODATA.
        constexpr double KelvinOffset = 273.15;
        constexpr double StepSafety = 0.2;
        constexpr int MaxSteps = 200000;

        bool ValidTemperature(double TemperatureC)
        {
            return std::isfinite(TemperatureC) &&
                std::isfinite(TemperatureC + KelvinOffset) &&
                TemperatureC + KelvinOffset > 0;
        }

        Result Failure(int Steps = 0)
        {
            Result R;
            R.Valid = false;
            R.Steps = Steps;
            return R;
        }

        bool Close(double A, double B, double Tolerance = 1e-8)
        {
            return std::isfinite(A) && std::isfinite(B) && std::abs(A - B) <= Tolerance;
        }
    }

    Result Advance(std::vector<Node>& Nodes, const std::vector<Link>& Links,
        double Seconds, double MaxStepSeconds)
    {
        if (!std::isfinite(Seconds) || Seconds < 0 ||
            !std::isfinite(MaxStepSeconds) || MaxStepSeconds <= 0)
        {
            return Failure();
        }
        for (const Node& N : Nodes)
        {
            if (!ValidTemperature(N.TemperatureC) ||
                !std::isfinite(N.CapacityJPerK) || N.CapacityJPerK <= 0)
            {
                return Failure();
            }
        }
        for (const Link& L : Links)
        {
            if (L.A < 0 || L.B < 0 || static_cast<std::size_t>(L.A) >= Nodes.size() ||
                static_cast<std::size_t>(L.B) >= Nodes.size() ||
                !std::isfinite(L.ConductanceWPerK) || L.ConductanceWPerK < 0 ||
                !std::isfinite(L.RadiationAreaM2) || L.RadiationAreaM2 < 0)
            {
                return Failure();
            }
        }
        Result R;
        if (Seconds == 0 || Nodes.empty()) return R;

        // Work privately until the whole requested interval succeeds.
        std::vector<double> Temperatures;
        Temperatures.reserve(Nodes.size());
        for (const Node& N : Nodes) Temperatures.push_back(N.TemperatureC);
        std::vector<double> SumG(Nodes.size(), 0);
        std::vector<double> DeltaEnergy(Nodes.size(), 0);
        std::vector<double> EdgeG(Links.size(), 0);
        long double SuppliedEnergy = 0;
        double Remaining = Seconds;

        while (Remaining > 0)
        {
            if (R.Steps >= MaxSteps) return Failure(R.Steps);
            std::fill(SumG.begin(), SumG.end(), 0);
            double Dt = std::min(Remaining, MaxStepSeconds);
            bool HasDynamicLink = false;
            for (std::size_t Edge = 0; Edge < Links.size(); ++Edge)
            {
                const Link& L = Links[Edge];
                EdgeG[Edge] = 0;
                // Self-links transfer nothing. Fixed-to-fixed exchange has zero
                // net reservoir supply and cannot change this network's state.
                if (L.A == L.B || (Nodes[L.A].Fixed && Nodes[L.B].Fixed)) continue;
                double G = L.ConductanceWPerK;
                if (L.RadiationAreaM2 > 0)
                {
                    const double Ta = Temperatures[L.A] + KelvinOffset;
                    const double Tb = Temperatures[L.B] + KelvinOffset;
                    // Exact secant: (Ta^4-Tb^4)=(Ta-Tb)(Ta+Tb)(Ta^2+Tb^2).
                    // This avoids cancellation when the temperatures are close.
                    G += Sigma * L.RadiationAreaM2 * (Ta + Tb) * (Ta * Ta + Tb * Tb);
                }
                if (!std::isfinite(G) || G < 0) return Failure(R.Steps);
                EdgeG[Edge] = G;
                SumG[L.A] += G;
                SumG[L.B] += G;
                if (!std::isfinite(SumG[L.A]) || !std::isfinite(SumG[L.B]))
                    return Failure(R.Steps);
                HasDynamicLink = HasDynamicLink || G > 0;
            }
            // No nonzero coupling to a dynamic node: unchanged at any duration.
            if (!HasDynamicLink) break;
            for (std::size_t I = 0; I < Nodes.size(); ++I)
            {
                if (!Nodes[I].Fixed && SumG[I] > 0)
                {
                    const double Bound = StepSafety * (Nodes[I].CapacityJPerK / SumG[I]);
                    Dt = std::min(Dt, Bound);
                }
            }
            // Reject numerical underflow or elapsed-time loss of significance.
            if (!std::isfinite(Dt) || Dt <= 0 || Remaining - Dt == Remaining)
                return Failure(R.Steps);

            std::fill(DeltaEnergy.begin(), DeltaEnergy.end(), 0);
            for (std::size_t Edge = 0; Edge < Links.size(); ++Edge)
            {
                const Link& L = Links[Edge];
                if (EdgeG[Edge] == 0) continue;
                const double Joules = (EdgeG[Edge] * Dt) *
                    (Temperatures[L.A] - Temperatures[L.B]);
                if (!std::isfinite(Joules)) return Failure(R.Steps);
                // One signed edge flux, not two independently rounded evaluations.
                DeltaEnergy[L.A] -= Joules;
                DeltaEnergy[L.B] += Joules;
            }
            long double StepSupply = 0;
            for (std::size_t I = 0; I < Nodes.size(); ++I)
            {
                if (!std::isfinite(DeltaEnergy[I])) return Failure(R.Steps);
                if (Nodes[I].Fixed)
                {
                    StepSupply -= static_cast<long double>(DeltaEnergy[I]);
                }
                else
                {
                    // All edge fluxes above used the previous temperatures.
                    const double Updated = Temperatures[I] + DeltaEnergy[I] / Nodes[I].CapacityJPerK;
                    if (!ValidTemperature(Updated)) return Failure(R.Steps);
                    Temperatures[I] = Updated;
                }
            }
            SuppliedEnergy += StepSupply;
            if (!std::isfinite(SuppliedEnergy)) return Failure(R.Steps);
            Remaining -= Dt;
            ++R.Steps;
        }

        long double PassiveEnergyChange = 0;
        for (std::size_t I = 0; I < Nodes.size(); ++I)
        {
            if (!Nodes[I].Fixed)
            {
                PassiveEnergyChange += static_cast<long double>(Nodes[I].CapacityJPerK) *
                    (static_cast<long double>(Temperatures[I]) - Nodes[I].TemperatureC);
            }
        }
        R.SuppliedEnergyJ = static_cast<double>(SuppliedEnergy);
        R.EnergyResidualJ = static_cast<double>(PassiveEnergyChange - SuppliedEnergy);
        if (!std::isfinite(R.SuppliedEnergyJ) || !std::isfinite(R.EnergyResidualJ))
            return Failure(R.Steps);
        for (std::size_t I = 0; I < Nodes.size(); ++I)
            Nodes[I].TemperatureC = Temperatures[I];
        return R;
    }

    bool SelfTest()
    {
        const std::vector<Link> Conductive = {{0, 1, 2, 0}};
        std::vector<Node> Pair = {{95, 100, false}, {24, 100, false}};
        const Result PairResult = Advance(Pair, Conductive, 20, 0.1);
        if (!PairResult.Valid || Pair[0].TemperatureC >= 95 || Pair[1].TemperatureC <= 24 ||
            !Close(Pair[0].TemperatureC + Pair[1].TemperatureC, 119) ||
            !Close(PairResult.EnergyResidualJ, 0) || PairResult.SuppliedEnergyJ != 0)
            return false;

        std::vector<Node> Equilibrium = {{37, 2, false}, {37, 7, false}, {37, 1, true}};
        const Result EquilibriumResult = Advance(Equilibrium, {{0, 1, 1, .1}, {1, 2, 2, .2}}, 2);
        if (!EquilibriumResult.Valid || EquilibriumResult.SuppliedEnergyJ != 0 ||
            EquilibriumResult.EnergyResidualJ != 0)
            return false;
        for (const Node& N : Equilibrium) if (N.TemperatureC != 37) return false;

        std::vector<Node> Unequal = {{95, 100, false}, {24, 300, false}};
        const Result UnequalResult = Advance(Unequal, Conductive, 2000);
        if (!UnequalResult.Valid || !Close(Unequal[0].TemperatureC, 41.75, 1e-7) ||
            !Close(Unequal[1].TemperatureC, 41.75, 1e-7) ||
            !Close(UnequalResult.EnergyResidualJ, 0, 1e-7)) return false;

        std::vector<Node> Thermostat = {{95, 1, true}, {24, 100, false}};
        const Result ThermostatResult = Advance(Thermostat, Conductive, 10, .01);
        const double ExactFixed = 95 + (24 - 95) * std::exp(-2.0 * 10 / 100);
        if (!ThermostatResult.Valid || Thermostat[0].TemperatureC != 95 ||
            !Close(Thermostat[1].TemperatureC, ExactFixed, .002) ||
            ThermostatResult.SuppliedEnergyJ <= 0 ||
            !Close(ThermostatResult.SuppliedEnergyJ, 100 * (Thermostat[1].TemperatureC - 24), 1e-7) ||
            !Close(ThermostatResult.EnergyResidualJ, 0, 1e-7)) return false;

        std::vector<Node> Radiative = {{95, 1, true}, {24, 100, false}};
        const Result RadiationResult = Advance(Radiative, {{0, 1, 0, .01}}, .01, .01);
        const double ExpectedJ = Sigma * .01 *
            (std::pow(95 + KelvinOffset, 4) - std::pow(24 + KelvinOffset, 4)) * .01;
        if (!RadiationResult.Valid || RadiationResult.Steps != 1 ||
            !Close(RadiationResult.SuppliedEnergyJ, ExpectedJ, 1e-12) ||
            !Close(Radiative[1].TemperatureC, 24 + ExpectedJ / 100, 1e-12) ||
            !Close(RadiationResult.EnergyResidualJ, 0, 1e-9)) return false;

        std::vector<Node> Disabled = {{95, 1, false}, {24, 1, false}};
        const Result DisabledResult = Advance(Disabled, {{0, 1, 0, 0}}, 1000000);
        if (!DisabledResult.Valid || DisabledResult.Steps != 0 ||
            Disabled[0].TemperatureC != 95 || Disabled[1].TemperatureC != 24) return false;

        std::vector<Node> Forward = {{95, 100, false}, {24, 300, false}};
        std::vector<Node> Reverse = {{24, 300, false}, {95, 100, false}};
        const Result ForwardResult = Advance(Forward, {{0, 1, 2, .01}}, 30, .1);
        const Result ReverseResult = Advance(Reverse, {{1, 0, 2, .01}}, 30, .1);
        if (!ForwardResult.Valid || !ReverseResult.Valid ||
            !Close(Forward[0].TemperatureC, Reverse[1].TemperatureC, 1e-12) ||
            !Close(Forward[1].TemperatureC, Reverse[0].TemperatureC, 1e-12)) return false;

        std::vector<Node> Coarse = {{95, 1, true}, {24, 100, false}};
        std::vector<Node> Fine = Coarse;
        const Result CoarseResult = Advance(Coarse, Conductive, 20, 1);
        const Result FineResult = Advance(Fine, Conductive, 20, .1);
        const double Exact20 = 95 + (24 - 95) * std::exp(-2.0 * 20 / 100);
        if (!CoarseResult.Valid || !FineResult.Valid ||
            std::abs(Fine[1].TemperatureC - Exact20) >= .2 * std::abs(Coarse[1].TemperatureC - Exact20))
            return false;

        std::vector<Node> Invalid = {{95, 100, false}, {24, 0, false}};
        if (Advance(Invalid, Conductive, 1).Valid || Invalid[0].TemperatureC != 95) return false;
        Invalid[1].CapacityJPerK = 100;
        if (Advance(Invalid, {{0, 2, 1, 0}}, 1).Valid ||
            Advance(Invalid, {{0, 1, -1, 0}}, 1).Valid ||
            Advance(Invalid, Conductive, std::numeric_limits<double>::quiet_NaN()).Valid ||
            Advance(Invalid, Conductive, 1, 0).Valid) return false;
        Invalid[1].TemperatureC = -273.15;
        return !Advance(Invalid, Conductive, 1).Valid && Invalid[0].TemperatureC == 95;
    }
}
