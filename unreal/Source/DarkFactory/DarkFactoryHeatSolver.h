#pragma once

#include <vector>

// Engine-independent, finite-capacity thermal network. Link areas already include
// visibility/view factors and any chosen emissivity approximation. Radiation uses
// total-spectrum Stefan-Boltzmann power, not the camera's 8-14 um radiance band.
namespace DarkFactoryHeat
{
    struct Node
    {
        double TemperatureC = 24;
        double CapacityJPerK = 1;
        bool Fixed = false;
    };

    struct Link
    {
        int A = 0;
        int B = 0;
        double ConductanceWPerK = 0;
        double RadiationAreaM2 = 0;
    };

    struct Result
    {
        bool Valid = true;
        int Steps = 0;
        double SuppliedEnergyJ = 0;
        double EnergyResidualJ = 0;
    };

    // Advances all non-fixed nodes together from the previous substep's state.
    // Temperatures must be finite and above absolute zero; capacities and the
    // maximum step must be finite and positive. Seconds/coefficients are finite
    // and nonnegative. Invalid endpoints, arithmetic or >200,000 substeps fail.
    // Failure is transactional: Nodes is unchanged and both energy fields are 0;
    // Steps reports completed tentative substeps. There are no hidden clamps.
    // SuppliedEnergyJ is net energy supplied by fixed-temperature reservoirs;
    // EnergyResidualJ = actual non-fixed energy change - SuppliedEnergyJ.
    Result Advance(std::vector<Node>& Nodes, const std::vector<Link>& Links,
        double Seconds, double MaxStepSeconds = 1.0);

    // Opt-in numerical invariant checks; the caller owns logging/reporting.
    bool SelfTest();
}
