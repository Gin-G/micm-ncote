#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <micm/process/arrhenius_rate_constant.hpp>
#include <micm/process/branched_rate_constant.hpp>
#include <micm/process/process.hpp>
#include <micm/process/ternary_chemical_activation_rate_constant.hpp>
#include <micm/process/troe_rate_constant.hpp>
#include <micm/process/tunneling_rate_constant.hpp>
#include <micm/process/user_defined_rate_constant.hpp>
#include <micm/solver/rosenbrock.hpp>
#include <micm/solver/state.hpp>
#include <micm/system/phase.hpp>
#include <micm/system/system.hpp>
#include <micm/util/constants.hpp>
#include <micm/util/matrix.hpp>
#include <utility>
#include <vector>

#include "oregonator.hpp"

constexpr size_t nsteps = 1000;

void writeCSV(
    const std::string& filename,
    const std::vector<std::string>& header,
    const std::vector<std::vector<double>>& data,
    const std::vector<double>& times
    )
{
  std::ofstream file(filename);
  if (file.is_open())
  {
    // Write column headers
    for (size_t i = 0; i < header.size(); ++i)
    {
      file << header[i];
      if (i < header.size() - 1)
      {
        file << ",";
      }
    }
    file << "\n";

    // Write data rows
    for (size_t i = 0; i < data.size(); ++i)
    {
      file << times[i] << ",";
      for (size_t j = 0; j < data[i].size(); ++j)
      {
        file << data[i][j];
        if (j < data[i].size() - 1)
        {
          file << ",";
        }
      }
      file << "\n";
    }
    file.close();
  }
  else
  {
    std::cerr << "Error opening file: " << filename << std::endl;
  }
}

using yields = std::pair<micm::Species, double>;

template<class T>
using SparseMatrixTest = micm::SparseMatrix<T>;

TEST(AnalyticalExamples, Troe)
{
  /*
   * A -> B, k1
   * B -> C, k2
   *
   * Copying the CAMP example: https://github.com/open-atmos/camp/blob/main/test/unit_rxn_data/test_rxn_troe.F90
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TroeRateConstant({ .k0_A_ = 4.0e-10 }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::TroeRateConstant({ .k0_A_ = 1.2e-3,
                                                                 .k0_B_ = 167,
                                                                 .k0_C_ = 3,
                                                                 .kinf_A_ = 136,
                                                                 .kinf_B_ = 5,
                                                                 .kinf_C_ = 24,
                                                                 .Fc_ = 0.9,
                                                                 .N_ = 0.8 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k_0 = 4.0e-10;
  double k_inf = 1;
  double k1 = k_0 * air_density / (1.0 + k_0 * air_density / k_inf) *
              pow(0.6, 1.0 / (1.0 + (1.0 / 1.0) * pow(log10(k_0 * air_density / k_inf), 2)));

  // B->C reaction rate
  k_0 = 1.2e-3 * exp(3.0 / temperature) * pow(temperature / 300.0, 167.0);
  k_inf = 136.0 * exp(24.0 / temperature) * pow(temperature / 300.0, 5.0);
  double k2 = k_0 * air_density / (1.0 + k_0 * air_density / k_inf) *
              pow(0.9, 1.0 / (1.0 + (1.0 / 0.8) * pow(log10(k_0 * air_density / k_inf), 2)));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  model_concentrations[0] = { 1, 0, 0 };
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    EXPECT_NEAR(k1, state.rate_constants_.AsVector()[0], 1e-8);
    EXPECT_NEAR(k2, state.rate_constants_.AsVector()[1], 1e-8);
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);
  writeCSV("model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], 1e-8)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-8)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-8)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, TroeSuperStiffButAnalytical)
{
  /*
   * A1 -> B, k1
   * A2 -> B, k1
   * A1 -> A2, k3 >>> k1
   * A2 -> A1, k4 >>> k1
   * B -> C, k2
   *
   */

  auto a1 = micm::Species("A1");
  auto a2 = micm::Species("A2");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a1, a2, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TroeRateConstant({ .k0_A_ = 4.0e-10 }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TroeRateConstant({ .k0_A_ = 4.0e-10 }))
                         .phase(gas_phase);

  micm::Process r3 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::TroeRateConstant({ .k0_A_ = 1.2e-3,
                                                                 .k0_B_ = 167,
                                                                 .k0_C_ = 3,
                                                                 .kinf_A_ = 136,
                                                                 .kinf_B_ = 5,
                                                                 .kinf_C_ = 24,
                                                                 .Fc_ = 0.9,
                                                                 .N_ = 0.8 }))
                         .phase(gas_phase);

  micm::Process r4 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(a2, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e10 }))
                         .phase(gas_phase);

  micm::Process r5 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(a1, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 0.9 * 4.0e10 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3, r4, r5 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k_0 = 4.0e-10;
  double k_inf = 1;
  double k1 = k_0 * air_density / (1.0 + k_0 * air_density / k_inf) *
              pow(0.6, 1.0 / (1.0 + (1.0 / 1.0) * pow(log10(k_0 * air_density / k_inf), 2)));

  // B->C reaction rate
  k_0 = 1.2e-3 * exp(3.0 / temperature) * pow(temperature / 300.0, 167.0);
  k_inf = 136.0 * exp(24.0 / temperature) * pow(temperature / 300.0, 5.0);
  double k2 = k_0 * air_density / (1.0 + k_0 * air_density / k_inf) *
              pow(0.9, 1.0 / (1.0 + (1.0 / 0.8) * pow(log10(k_0 * air_density / k_inf), 2)));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(4));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  state.SetConcentration(a1, 0.5);
  state.SetConcentration(a2, 0.5);
  state.SetConcentration(b, 0.0);
  state.SetConcentration(c, 0.0);

  model_concentrations[0] = state.variables_[0];
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  auto header = state.variable_names_;
  header.insert(header.begin(), "time");
  writeCSV("stiff_model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a1 = map.at("A1");
  size_t _a2 = map.at("A2");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a1] + model_concentrations[i][_a2], analytical_concentrations[i][0], 1e-4);
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-4);
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-4);
  }
}

TEST(AnalyticalExamples, Photolysis)
{
  /*
   * A -> B, k1
   * B -> C, k2
   *
   * Copying the CAMP example: https://github.com/open-atmos/camp/blob/main/test/unit_rxn_data/test_rxn_photolysis.F90
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "photoA" }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "photoB" }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k1 = 2e-3;

  // B->C reaction rate
  double k2 = 3e-3;

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  state.SetCustomRateParameter("photoA", k1);
  state.SetCustomRateParameter("photoB", k2);

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  model_concentrations[0] = { 1, 0, 0 };
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    EXPECT_NEAR(k1, state.rate_constants_.AsVector()[0], 1e-8);
    EXPECT_NEAR(k2, state.rate_constants_.AsVector()[1], 1e-8);
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);
  writeCSV("model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], 1e-8)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-8)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-8)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, PhotolysisSuperStiffButAnalytical)
{
  /*
   * A1 -> B, k1
   * A2 -> B, k1
   * A1 -> A2, k3 >>> k1
   * A2 -> A1, k4 >>> k1
   * B -> C, k2
   *
   */

  auto a1 = micm::Species("A1");
  auto a2 = micm::Species("A2");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a1, a2, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "photoA1B" }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "photoA2B" }))
                         .phase(gas_phase);

  micm::Process r3 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "photoB" }))
                         .phase(gas_phase);

  micm::Process r4 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(a2, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e10 }))
                         .phase(gas_phase);

  micm::Process r5 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(a1, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 0.9 * 4.0e10 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3, r4, r5 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k1 = 2e-3;

  // B->C reaction rate
  double k2 = 3e-3;

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  state.SetCustomRateParameter("photoA1B", k1);
  state.SetCustomRateParameter("photoA2B", k1);
  state.SetCustomRateParameter("photoB", k2);

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  state.SetConcentration(a1, 0.5);
  state.SetConcentration(a2, 0.5);
  state.SetConcentration(b, 0.0);
  state.SetConcentration(c, 0.0);

  model_concentrations[0] = state.variables_[0];

  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);
  writeCSV("model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a1 = map.at("A1");
  size_t _a2 = map.at("A2");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a1] + model_concentrations[i][_a2], analytical_concentrations[i][0], 1e-4);
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-4);
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-4);
  }
}

TEST(AnalyticalExamples, TernaryChemicalActivation)
{
  /*
   * A -> B, k1
   * B -> C, k2
   *
   * Copying the CAMP example:
   * https://github.com/open-atmos/camp/blob/main/test/unit_rxn_data/test_rxn_ternary_chemical_activation.F90
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TernaryChemicalActivationRateConstant({ .k0_A_ = 4.0e-10, .kinf_A_ = 1 }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::TernaryChemicalActivationRateConstant({ .k0_A_ = 1.2e-3,
                                                                                      .k0_B_ = 167,
                                                                                      .k0_C_ = 3,
                                                                                      .kinf_A_ = 136,
                                                                                      .kinf_B_ = 5,
                                                                                      .kinf_C_ = 24,
                                                                                      .Fc_ = 0.9,
                                                                                      .N_ = 0.8 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k_0 = 4.0e-10;
  double k_inf = 1;
  double k1 = k_0 / (1.0 + k_0 * air_density / k_inf) *
              pow(0.6, 1.0 / (1.0 + (1.0 / 1.0) * pow(log10(k_0 * air_density / k_inf), 2)));

  // B->C reaction rate
  k_0 = 1.2e-3 * exp(3.0 / temperature) * pow(temperature / 300.0, 167.0);
  k_inf = 136.0 * exp(24.0 / temperature) * pow(temperature / 300.0, 5.0);
  double k2 = k_0 / (1.0 + k_0 * air_density / k_inf) *
              pow(0.9, 1.0 / (1.0 + (1.0 / 0.8) * pow(log10(k_0 * air_density / k_inf), 2)));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  model_concentrations[0] = { 1, 0, 0 };
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    EXPECT_NEAR(k1, state.rate_constants_.AsVector()[0], 1e-8);
    EXPECT_NEAR(k2, state.rate_constants_.AsVector()[1], 1e-8);
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);
  writeCSV("model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], 1e-8)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-8)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-8)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, TernaryChemicalActivationSuperStiffButAnalytical)
{
  /*
   * A1 -> B, k1
   * A2 -> B, k1
   * A1 -> A2, k3 >>> k1
   * A2 -> A1, k4 >>> k1
   * B -> C, k2
   *
   */

  auto a1 = micm::Species("A1");
  auto a2 = micm::Species("A2");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a1, a2, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TernaryChemicalActivationRateConstant({ .k0_A_ = 4.0e-10, .kinf_A_ = 1 }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TernaryChemicalActivationRateConstant({ .k0_A_ = 4.0e-10, .kinf_A_ = 1 }))
                         .phase(gas_phase);

  micm::Process r3 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::TernaryChemicalActivationRateConstant({ .k0_A_ = 1.2e-3,
                                                                                      .k0_B_ = 167,
                                                                                      .k0_C_ = 3,
                                                                                      .kinf_A_ = 136,
                                                                                      .kinf_B_ = 5,
                                                                                      .kinf_C_ = 24,
                                                                                      .Fc_ = 0.9,
                                                                                      .N_ = 0.8 }))
                         .phase(gas_phase);

  micm::Process r4 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(a2, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e10 }))
                         .phase(gas_phase);

  micm::Process r5 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(a1, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 0.9 * 4.0e10 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3, r4, r5 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k_0 = 4.0e-10;
  double k_inf = 1;
  double k1 = k_0 / (1.0 + k_0 * air_density / k_inf) *
              pow(0.6, 1.0 / (1.0 + (1.0 / 1.0) * pow(log10(k_0 * air_density / k_inf), 2)));

  // B->C reaction rate
  k_0 = 1.2e-3 * exp(3.0 / temperature) * pow(temperature / 300.0, 167.0);
  k_inf = 136.0 * exp(24.0 / temperature) * pow(temperature / 300.0, 5.0);
  double k2 = k_0 / (1.0 + k_0 * air_density / k_inf) *
              pow(0.9, 1.0 / (1.0 + (1.0 / 0.8) * pow(log10(k_0 * air_density / k_inf), 2)));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(4));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  state.SetConcentration(a1, 0.5);
  state.SetConcentration(a2, 0.5);
  state.SetConcentration(b, 0.0);
  state.SetConcentration(c, 0.0);

  model_concentrations[0] = state.variables_[0];
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  auto header = state.variable_names_;
  header.insert(header.begin(), "time");
  writeCSV("stiff_model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a1 = map.at("A1");
  size_t _a2 = map.at("A2");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a1] + model_concentrations[i][_a2], analytical_concentrations[i][0], 1e-4);
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-4);
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-4);
  }
}

TEST(AnalyticalExamples, Tunneling)
{
  /*
   * A -> B, k1
   * B -> C, k2
   *
   * Copying the CAMP example:
   * https://github.com/open-atmos/camp/blob/main/test/unit_rxn_data/test_rxn_wennberg_tunneling.F90
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TunnelingRateConstant({ .A_ = 4.0e-3 }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::TunnelingRateConstant({ .A_ = 1.2e-4, .B_ = 167, .C_ = 1.0e8 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k1 = 4.0e-3;

  // B->C reaction rate
  double k2 = 1.2e-4 * exp(-167 / temperature + 1.0e8 / pow(temperature, 3));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  model_concentrations[0] = { 1, 0, 0 };
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    EXPECT_NEAR(k1, state.rate_constants_.AsVector()[0], 1e-8);
    EXPECT_NEAR(k2, state.rate_constants_.AsVector()[1], 1e-8);
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);
  writeCSV("model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], 1e-8)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-8)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-8)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, TunnelingSuperStiffButAnalytical)
{
  /*
   * A1 -> B, k1
   * A2 -> B, k1
   * A1 -> A2, k3 >>> k1
   * A2 -> A1, k4 >>> k1
   * B -> C, k2
   *
   */

  auto a1 = micm::Species("A1");
  auto a2 = micm::Species("A2");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a1, a2, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TunnelingRateConstant({ .A_ = 4.0e-3 }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::TunnelingRateConstant({ .A_ = 4.0e-3 }))
                         .phase(gas_phase);

  micm::Process r3 = micm::Process::create()
                         .reactants({ b })
                         .products({ yields(c, 1) })
                         .rate_constant(micm::TunnelingRateConstant({ .A_ = 1.2e-4, .B_ = 167, .C_ = 1.0e8 }))
                         .phase(gas_phase);

  micm::Process r4 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(a2, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e10 }))
                         .phase(gas_phase);

  micm::Process r5 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(a1, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 0.9 * 4.0e10 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3, r4, r5 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k1 = 4.0e-3;

  // B->C reaction rate
  double k2 = 1.2e-4 * exp(-167 / temperature + 1.0e8 / pow(temperature, 3));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(4));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  state.SetConcentration(a1, 0.5);
  state.SetConcentration(a2, 0.5);
  state.SetConcentration(b, 0.0);
  state.SetConcentration(c, 0.0);

  model_concentrations[0] = state.variables_[0];
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  auto header = state.variable_names_;
  header.insert(header.begin(), "time");
  writeCSV("stiff_model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a1 = map.at("A1");
  size_t _a2 = map.at("A2");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a1] + model_concentrations[i][_a2], analytical_concentrations[i][0], 1e-4)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-4)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-4)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, Arrhenius)
{
  /*
   * A -> B, k1
   * B -> C, k2
   *
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e-3, .C_ = 50 }))
                         .phase(gas_phase);

  micm::Process r2 =
      micm::Process::create()
          .reactants({ b })
          .products({ yields(c, 1) })
          .rate_constant(micm::ArrheniusRateConstant({ .A_ = 1.2e-4, .B_ = 167, .C_ = 75, .D_ = 50, .E_ = 0.5 }))
          .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k1 = 4.0e-3 * std::exp(50 / temperature);

  // B->C reaction rate
  double k2 = 1.2e-4 * std::exp(75 / temperature) * pow(temperature / 50, 167) * (1.0 + 0.5 * pressure);

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  model_concentrations[0] = { 1, 0, 0 };
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    EXPECT_NEAR(k1, state.rate_constants_.AsVector()[0], 1e-8);
    EXPECT_NEAR(k2, state.rate_constants_.AsVector()[1], 1e-8);
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);
  writeCSV("model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], 1e-8)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-8)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-8)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, ArrheniusSuperStiffButAnalytical)
{
  /*
   * A1 -> B, k1
   * A2 -> B, k1
   * A1 -> A2, k3 >>> k1
   * A2 -> A1, k4 >>> k1
   * B -> C, k2
   *
   */

  auto a1 = micm::Species("A1");
  auto a2 = micm::Species("A2");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a1, a2, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e-3, .C_ = 50 }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e-3, .C_ = 50 }))
                         .phase(gas_phase);

  micm::Process r3 =
      micm::Process::create()
          .reactants({ b })
          .products({ yields(c, 1) })
          .rate_constant(micm::ArrheniusRateConstant({ .A_ = 1.2e-4, .B_ = 167, .C_ = 75, .D_ = 50, .E_ = 0.5 }))
          .phase(gas_phase);

  micm::Process r4 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(a2, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e10 }))
                         .phase(gas_phase);

  micm::Process r5 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(a1, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 0.9 * 4.0e10 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3, r4, r5 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double k1 = 4.0e-3 * std::exp(50 / temperature);

  // B->C reaction rate
  double k2 = 1.2e-4 * std::exp(75 / temperature) * pow(temperature / 50, 167) * (1.0 + 0.5 * pressure);

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(4));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  state.SetConcentration(a1, 0.5);
  state.SetConcentration(a2, 0.5);
  state.SetConcentration(b, 0.0);
  state.SetConcentration(c, 0.0);

  model_concentrations[0] = state.variables_[0];
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  auto header = state.variable_names_;
  header.insert(header.begin(), "time");
  writeCSV("stiff_model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a1 = map.at("A1");
  size_t _a2 = map.at("A2");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a1] + model_concentrations[i][_a2], analytical_concentrations[i][0], 1e-4)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-4)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-4)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, Branched)
{
  /*
   * A -> B, k1
   * B -> C, k2
   *
   * Copying the CAMP example: https://github.com/open-atmos/camp/blob/main/test/unit_rxn_data/test_rxn_wennberg_no_ro2.F90
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 =
      micm::Process::create()
          .reactants({ a })
          .products({ yields(b, 1) })
          .rate_constant(micm::BranchedRateConstant({ .branch_ = micm::BranchedRateConstantParameters::Branch::Alkoxy,
                                                      .X_ = 1.2,
                                                      .Y_ = 204.3,
                                                      .a0_ = 1.0e-3,
                                                      .n_ = 2 }))
          .phase(gas_phase);

  micm::Process r2 =
      micm::Process::create()
          .reactants({ b })
          .products({ yields(c, 1) })
          .rate_constant(micm::BranchedRateConstant({ .branch_ = micm::BranchedRateConstantParameters::Branch::Nitrate,
                                                      .X_ = 1.2,
                                                      .Y_ = 204.3,
                                                      .a0_ = 1.0e-3,
                                                      .n_ = 2 }))
          .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double air_dens_n_cm3 = air_density * AVOGADRO_CONSTANT * 1.0e-6;
  double a_ = 2.0e-22 * std::exp(2) * 2.45e19;
  double b_ = 0.43 * std::pow((293.0 / 298.0), -8.0);
  double z = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2))) * (1.0 - 1.0e-3) / 1.0e-3;
  a_ = 2.0e-22 * std::exp(2) * air_dens_n_cm3;
  b_ = 0.43 * std::pow((temperature / 298.0), -8.0);
  double A = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2)));

  double k1 = 1.2 * std::exp(-204.3 / temperature) * (z / (z + A));

  // B->C reaction rate
  a_ = 2.0e-22 * std::exp(2) * 2.45e19;
  b_ = 0.43 * std::pow((293.0 / 298.0), -8.0);
  z = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2))) * (1.0 - 1.0e-3) / 1.0e-3;
  a_ = 2.0e-22 * std::exp(2) * air_dens_n_cm3;
  b_ = 0.43 * std::pow((temperature / 298.0), -8.0);
  A = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2)));

  double k2 = 1.2 * std::exp(-204.3 / temperature) * (A / (z + A));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  model_concentrations[0] = { 1, 0, 0 };
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    EXPECT_NEAR(k1, state.rate_constants_.AsVector()[0], 1e-8);
    EXPECT_NEAR(k2, state.rate_constants_.AsVector()[1], 1e-8);
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);
  writeCSV("model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], 1e-3)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-3)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-3)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, BranchedSuperStiffButAnalytical)
{
  /*
   * A1 -> B, k1
   * A2 -> B, k1
   * A1 -> A2, k3 >>> k1
   * A2 -> A1, k4 >>> k1
   * B -> C, k2
   *
   */

  auto a1 = micm::Species("A1");
  auto a2 = micm::Species("A2");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a1, a2, b, c } };

  micm::Process r1 =
      micm::Process::create()
          .reactants({ a1 })
          .products({ yields(b, 1) })
          .rate_constant(micm::BranchedRateConstant({ .branch_ = micm::BranchedRateConstantParameters::Branch::Alkoxy,
                                                      .X_ = 1.2,
                                                      .Y_ = 204.3,
                                                      .a0_ = 1.0e-3,
                                                      .n_ = 2 }))
          .phase(gas_phase);

  micm::Process r2 =
      micm::Process::create()
          .reactants({ a2 })
          .products({ yields(b, 1) })
          .rate_constant(micm::BranchedRateConstant({ .branch_ = micm::BranchedRateConstantParameters::Branch::Alkoxy,
                                                      .X_ = 1.2,
                                                      .Y_ = 204.3,
                                                      .a0_ = 1.0e-3,
                                                      .n_ = 2 }))
          .phase(gas_phase);

  micm::Process r3 =
      micm::Process::create()
          .reactants({ b })
          .products({ yields(c, 1) })
          .rate_constant(micm::BranchedRateConstant({ .branch_ = micm::BranchedRateConstantParameters::Branch::Nitrate,
                                                      .X_ = 1.2,
                                                      .Y_ = 204.3,
                                                      .a0_ = 1.0e-3,
                                                      .n_ = 2 }))
          .phase(gas_phase);

  micm::Process r4 = micm::Process::create()
                         .reactants({ a1 })
                         .products({ yields(a2, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 4.0e10 }))
                         .phase(gas_phase);

  micm::Process r5 = micm::Process::create()
                         .reactants({ a2 })
                         .products({ yields(a1, 1) })
                         .rate_constant(micm::ArrheniusRateConstant({ .A_ = 0.9 * 4.0e10 }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3, r4, r5 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  // A->B reaction rate
  double air_dens_n_cm3 = air_density * AVOGADRO_CONSTANT * 1.0e-6;
  double a_ = 2.0e-22 * std::exp(2) * 2.45e19;
  double b_ = 0.43 * std::pow((293.0 / 298.0), -8.0);
  double z = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2))) * (1.0 - 1.0e-3) / 1.0e-3;
  a_ = 2.0e-22 * std::exp(2) * air_dens_n_cm3;
  b_ = 0.43 * std::pow((temperature / 298.0), -8.0);
  double A = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2)));

  double k1 = 1.2 * std::exp(-204.3 / temperature) * (z / (z + A));

  // B->C reaction rate
  a_ = 2.0e-22 * std::exp(2) * 2.45e19;
  b_ = 0.43 * std::pow((293.0 / 298.0), -8.0);
  z = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2))) * (1.0 - 1.0e-3) / 1.0e-3;
  a_ = 2.0e-22 * std::exp(2) * air_dens_n_cm3;
  b_ = 0.43 * std::pow((temperature / 298.0), -8.0);
  A = a_ / (1.0 + a_ / b_) * std::pow(0.41, 1.0 / (1.0 + std::pow(std::log10(a_ / b_), 2)));

  double k2 = 1.2 * std::exp(-204.3 / temperature) * (A / (z + A));

  double time_step = 1.0;
  micm::State<micm::Matrix> state = solver.GetState();

  std::vector<std::vector<double>> model_concentrations(nsteps, std::vector<double>(4));
  std::vector<std::vector<double>> analytical_concentrations(nsteps, std::vector<double>(3));

  state.SetConcentration(a1, 0.5);
  state.SetConcentration(a2, 0.5);
  state.SetConcentration(b, 0.0);
  state.SetConcentration(c, 0.0);

  model_concentrations[0] = state.variables_[0];
  analytical_concentrations[0] = { 1, 0, 0 };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time < nsteps; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();

    // Analytical results
    double time = i_time * time_step;

    double initial_A = analytical_concentrations[0][idx_A];
    analytical_concentrations[i_time][idx_A] = initial_A * std::exp(-(k1)*time);
    analytical_concentrations[i_time][idx_B] = initial_A * (k1 / (k2 - k1)) * (std::exp(-k1 * time) - std::exp(-k2 * time));

    analytical_concentrations[i_time][idx_C] =
        initial_A * (1.0 + (k1 * std::exp(-k2 * time) - k2 * std::exp(-k1 * time)) / (k2 - k1));
  }

  auto header = state.variable_names_;
  header.insert(header.begin(), "time");
  writeCSV("stiff_model_concentrations.csv", header, model_concentrations, times);

  auto map = state.variable_map_;

  size_t _a1 = map.at("A1");
  size_t _a2 = map.at("A2");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a1] + model_concentrations[i][_a2], analytical_concentrations[i][0], 1e-3)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], 1e-3)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], 1e-3)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, Robertson)
{
  /*
   * A -> B, k1 = 0.04
   * B + B -> C + B, k2 = 3e7
   * B + C -> A + C, k3 = 1e4
   *
   * this problem is described in
   * Hairer, E., Wanner, G., 1996. Solving Ordinary Differential Equations II: Stiff and Differential-Algebraic Problems, 2nd
   * edition. ed. Springer, Berlin ; New York. Page 3
   *
   * solutions are provided here https://www.unige.ch/~hairer/testset/stiff/rober/res_exact_pic
   * https://www.unige.ch/~hairer/testset/testset.html
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "r1" }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ b, b })
                         .products({ yields(b, 1), yields(c, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "r2" }))
                         .phase(gas_phase);

  micm::Process r3 = micm::Process::create()
                         .reactants({ b, c })
                         .products({ yields(a, 1), yields(c, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "r3" }))
                         .phase(gas_phase);

  micm::RosenbrockSolver<micm::Matrix, SparseMatrixTest> solver{
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3 },
    micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters()
  };

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  micm::State<micm::Matrix> state = solver.GetState();

  double k1 = 0.04;
  double k2 = 3e7;
  double k3 = 1e4;

  state.SetCustomRateParameter("r1", k1);
  state.SetCustomRateParameter("r2", k2);
  state.SetCustomRateParameter("r3", k3);

  constexpr size_t N = 12;

  std::vector<std::vector<double>> model_concentrations(N + 1, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(N + 1, std::vector<double>(3));

  model_concentrations[0] = { 1, 0, 0 };

  analytical_concentrations = { { 1, 0, 0 },
                                { 0.9664597373330035E+00, 0.3074626578578675E-04, 0.3350951640121071E-01 },
                                { 0.8413699238414729E+00, 0.1623390937990473E-04, 0.1586138422491472E+00 },
                                { 0.6172348823960878E+00, 0.6153591274639123E-05, 0.3827589640126376E+00 },
                                { 0.3368745306607069E+00, 0.2013702318261393E-05, 0.6631234556369748E+00 },
                                { 0.1073004285378040E+00, 0.4800166972571660E-06, 0.8926990914454987E+00 },
                                { 0.1786592114209946E-01, 0.7274751468436319E-07, 0.9821340061103859E+00 },
                                { 0.2031483924973415E-02, 0.8142277783356159E-08, 0.9979685079327488E+00 },
                                { 0.2076093439016395E-03, 0.8306077485067610E-09, 0.9997923898254906E+00 },
                                { 0.2082417512179460E-04, 0.8329841429908955E-10, 0.9999791757415798E+00 },
                                { 0.2083229471647004E-05, 0.8332935037760723E-11, 0.9999979167621954E+00 },
                                { 0.2083328471883087E-06, 0.8333315602809495E-12, 0.9999997916663195E+00 },
                                { 0.2083340149701284E-07, 0.8333360770334744E-13, 0.9999999791665152E+00 } };

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  double time_step = 1.0;
  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 1; i_time <= N; ++i_time)
  {
    times.push_back(time_step);
    // Model results
    auto result = solver.Solve(time_step, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    EXPECT_NEAR(k1, state.rate_constants_.AsVector()[0], 1e-8);
    EXPECT_NEAR(k2, state.rate_constants_.AsVector()[1], 1e-8);
    EXPECT_NEAR(k3, state.rate_constants_.AsVector()[2], 1e-8);
    model_concentrations[i_time] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();
    time_step *= 10;
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("model_concentrations.csv", header, model_concentrations, times);
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  double tol = 1e-1;
  for (size_t i = 0; i < model_concentrations.size(); ++i)
  {
    EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], tol)
        << "Arrays differ at index (" << i << ", " << 0 << ")";
    EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], tol)
        << "Arrays differ at index (" << i << ", " << 1 << ")";
    EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], tol)
        << "Arrays differ at index (" << i << ", " << 2 << ")";
  }
}

TEST(AnalyticalExamples, Oregonator)
{
  /*
   * I think these are the equations, but I'm really not sure
   * A+Y -> X+P
   * X+Y -> 2P
   * A+X -> 2X+2Z
   * 2X -> A+P
   * B+Z -> 1/2fY
   *
   * this problem is described in
   * Hairer, E., Wanner, G., 1996. Solving Ordinary Differential Equations II: Stiff and Differential-Algebraic Problems, 2nd
   * edition. ed. Springer, Berlin ; New York. Page 3
   *
   * solutions are provided here https://www.unige.ch/~hairer/testset/stiff/rober/res_exact_pic
   * https://www.unige.ch/~hairer/testset/testset.html
   */

  auto a = micm::Species("A");
  auto b = micm::Species("B");
  auto c = micm::Species("C");

  micm::Phase gas_phase{ std::vector<micm::Species>{ a, b, c } };

  micm::Process r1 = micm::Process::create()
                         .reactants({ a })
                         .products({ yields(b, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "r1" }))
                         .phase(gas_phase);

  micm::Process r2 = micm::Process::create()
                         .reactants({ b, b })
                         .products({ yields(b, 1), yields(c, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "r2" }))
                         .phase(gas_phase);

  micm::Process r3 = micm::Process::create()
                         .reactants({ b, c })
                         .products({ yields(a, 1), yields(c, 1) })
                         .rate_constant(micm::UserDefinedRateConstant({ .label_ = "r3" }))
                         .phase(gas_phase);

  auto params = micm::RosenbrockSolverParameters::three_stage_rosenbrock_parameters();
  params.relative_tolerance_ = 1e-6;
  params.absolute_tolerance_ = 1e-6 * params.relative_tolerance_;
  Oregonator<micm::Matrix, SparseMatrixTest> solver(
    micm::System(micm::SystemParameters{ .gas_phase_ = gas_phase }),
    std::vector<micm::Process>{ r1, r2, r3 },
    params
  );

  double temperature = 272.5;
  double pressure = 101253.3;
  double air_density = 1e6;

  double end = 360;
  double time_step = 10;
  size_t N = static_cast<size_t>(end / time_step);

  std::vector<std::vector<double>> model_concentrations(N + 1, std::vector<double>(3));
  std::vector<std::vector<double>> analytical_concentrations(13, std::vector<double>(3));

  model_concentrations[0] = { 1, 2, 3 };

  analytical_concentrations = {
    { 1, 2, 3 },
    { 0.1000661467180497E+01, 0.1512778937348249E+04, 0.1035854312767229E+05 },
    { 0.1000874625199626E+01, 0.1144336972384497E+04, 0.8372149966624639E+02 },
    { 0.1001890368438751E+01, 0.5299926232295553E+03, 0.1662279579042420E+01 },
    { 0.1004118022612645E+01, 0.2438326079910346E+03, 0.1008822224048647E+01 },
    { 0.1008995416634061E+01, 0.1121664388662539E+03, 0.1007783229065319E+01 },
    { 0.1019763472537298E+01, 0.5159761322947535E+02, 0.1016985778956374E+01 },
    { 0.1043985088527474E+01, 0.2373442027531524E+02, 0.1037691843544522E+01 },
    { 0.1100849071667922E+01, 0.1091533805469020E+02, 0.1085831969810860E+01 },
    { 0.1249102130020572E+01, 0.5013945178605446E+01, 0.1208326626237875E+01 },
    { 0.1779724751937019E+01, 0.2281852385542403E+01, 0.1613754023671725E+01 },
    { 0.1000889326903503E+01, 0.1125438585746596E+04, 0.1641049483777168E+05 },
    { 0.1000814870318523E+01, 0.1228178521549889E+04, 0.1320554942846513E+03 },
  };

  micm::State<micm::Matrix> state = solver.GetState();

  state.variables_[0] = model_concentrations[0];
  state.conditions_[0].temperature_ = temperature;
  state.conditions_[0].pressure_ = pressure;
  state.conditions_[0].air_density_ = air_density;

  size_t idx_A = 0, idx_B = 1, idx_C = 2;

  std::vector<double> times;
  times.push_back(0);
  for (size_t i_time = 0; i_time < N; ++i_time)
  {
    double solve_time = time_step + i_time*time_step;
    times.push_back(solve_time);
    // Model results
    auto result = solver.Solve(solve_time, state);
    EXPECT_EQ(result.state_, (micm::SolverState::Converged));
    model_concentrations[i_time+1] = result.result_.AsVector();
    state.variables_[0] = result.result_.AsVector();
  }

  std::vector<std::string> header = { "time", "A", "B", "C" };
  writeCSV("model_concentrations.csv", header, model_concentrations, times);
  std::vector<double> an_times;
  an_times.push_back(0);
  for(int i = 1; i <= N; ++i){
    an_times.push_back(30 * i);
  }
  writeCSV("analytical_concentrations.csv", header, analytical_concentrations, an_times);

  auto map = state.variable_map_;

  size_t _a = map.at("A");
  size_t _b = map.at("B");
  size_t _c = map.at("C");

  // double tol = 1e-1;
  // for (size_t i = 0; i < model_concentrations.size(); ++i)
  // {
  //   EXPECT_NEAR(model_concentrations[i][_a], analytical_concentrations[i][0], tol)
  //       << "Arrays differ at index (" << i << ", " << 0 << ")";
  //   EXPECT_NEAR(model_concentrations[i][_b], analytical_concentrations[i][1], tol)
  //       << "Arrays differ at index (" << i << ", " << 1 << ")";
  //   EXPECT_NEAR(model_concentrations[i][_c], analytical_concentrations[i][2], tol)
  //       << "Arrays differ at index (" << i << ", " << 2 << ")";
  // }
}
