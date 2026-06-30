Implement `/app/solution.cpp`. It receives query points `(x,y,t)` and outputs
temperatures for a transient 2D heat-diffusion problem on the unit square.

The heat source, boundary values, initial condition, and material coefficients
are available through `thermal_oracle.hpp`. The oracle is black-box but
deterministic, and may contain time structure not visible from a few samples.
Sample the oracle as needed and choose a stable discretization that resolves the
relevant temporal and spatial scales.

The verifier compares your output against a trusted manufactured reference at
hidden points with tight tolerance, under a wall-clock budget.
