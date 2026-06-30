#pragma once

double heat_source(double x, double y, double t);
double initial_temp(double x, double y);
double boundary_value(double x, double y, double t);
double conductivity_x(double x);
double conductivity_y(double y);
double rho_cp();
double domain_T();
