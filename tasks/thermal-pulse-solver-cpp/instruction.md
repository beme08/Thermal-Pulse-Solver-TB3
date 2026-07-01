Implement `/app/solution.cpp`. It receives query points `(x,y,t)` and outputs
temperatures for a transient 2D heat-diffusion problem on the unit square.

The heat source, boundary values, initial condition, and material coefficients
are available through `thermal_oracle.hpp`. The oracle is black-box but
deterministic, and may contain time structure not visible from a few samples.
Sample the oracle as needed and choose a stable discretization that resolves the
relevant temporal and spatial scales.

The grader may evaluate multiple deterministic private cases in one run under
one shared wall-clock budget. Efficient per-case resolution selection matters:
blindly over-resolving every case can exceed the budget even when a stable
implicit solve with the right time scale fits.

Input:

```json
{ "points": [ {"x": 0.25, "y": 0.5, "t": 0.75} ] }
```

Output:

```json
{ "temperatures": [1.234] }
```

The output array must have the same length and order as the input points. Values
must be finite numbers, and the output object must not contain extra keys.
