#include "settings_functions.hpp"
#include <sys/sysinfo.h>

extern "C" { 
  void f_set_default_tol(bool new_tol);
}

static int getTotalRamGB() {
  struct sysinfo info;
  if (sysinfo(&info) != 0) return 0; // unknown
  unsigned long long bytes =
      static_cast<unsigned long long>(info.totalram) * info.mem_unit;
  unsigned long long gb = bytes / (1024ULL * 1024ULL * 1024ULL);
  return static_cast<int>(gb);
}

static int memoryCapFromRamGB(int ram_gb) {
  if (ram_gb <= 0) return 200;          // conservative fallback
  if (ram_gb <= 16) return 200;
  if (ram_gb <= 64) return 500;
  if (ram_gb <= 256) return 1000;
  return 2000;
}

static int parseBatchMode(const std::string& s) {
  if (s == "adaptive_cpu") return BATCH_ADAPTIVE_CPU;
  if (s == "adaptive_cpu_mem") return BATCH_ADAPTIVE_CPU_MEM;
  return BATCH_FIXED;
}

int Settings::readSettings() {
  std::ifstream settings_file(json_file_);
  json json_settings;
  if (!settings_file.good()) {
    std::cout << "Could not open settings file: " << json_file_ << 
                  "\n\tContinuing with default settings\n";
  } else {
    settings_file >> json_settings;
  }
  settings_file.close();

  distributed_settings_ = DistributedSettings(
    getSettings<bool>(json_settings, "Distributed_Settings", "distributed_mode")
        .value_or(false),
    getSettingsArray(json_settings, "Distributed_Settings", "servers_list")
        .value_or(std::vector<std::string> {}),
    getSettings<int>(json_settings, "Distributed_Settings", "port")
        .value_or(0),
    getSettings<int>(json_settings, "Distributed_Settings", "total_hru_count")
        .value_or(0),
    getSettings<int>(json_settings, "Distributed_Settings", "num_hru_per_batch")
        .value_or(0)
  );

  summa_actor_settings_ = SummaActorSettings(
    getSettings<int>(json_settings, "Summa_Actor", "max_gru_per_job")
        .value_or(GRU_PER_JOB),
    getSettings<bool>(json_settings, "Summa_Actor", "enable_logging")
        .value_or(false),
    getSettings<std::string>(json_settings, "Summa_Actor", "log_dir")
        .value_or("")
  );

  fa_actor_settings_ = FileAccessActorSettings(
    getSettings<int>(json_settings, "File_Access_Actor", 
        "num_partitions_in_output_buffer").value_or(NUM_PARTITIONS),
    getSettings<int>(json_settings, "File_Access_Actor", 
        "num_timesteps_in_output_buffer").value_or(OUTPUT_TIMESTEPS),
    getSettings<std::string>(json_settings,"File_Access_Actor", 
        "output_file_suffix").value_or("")
  );

  job_actor_settings_ = JobActorSettings(
    getSettings<std::string>(json_settings, "Job_Actor", "file_manager_path")
        .value_or(""),
    getSettings<int>(json_settings, "Job_Actor", "max_run_attempts")
        .value_or(1),
    getSettings<bool>(json_settings, "Job_Actor", "data_assimilation_mode")
        .value_or(false),
    getSettings<int>(json_settings, "Job_Actor", "batch_size")
        .value_or(MISSING_INT)
  );

  batching_settings_present_ = (json_settings.find("Batching") != json_settings.end());
  if(batching_settings_present_) {
    batch_settings_ = BatchSettings(
        parseBatchMode(getSettings<std::string>(json_settings, "Batching", "mode")
            .value_or("fixed")),
        getSettings<int>(json_settings, "Batching", "fixed_batch_size")
            .value_or(MISSING_INT),
        getSettings<double>(json_settings, "Batching", "alpha")
            .value_or(4),
        getSettings<double>(json_settings, "Batching", "beta")
            .value_or(16),
        getSettings<int>(json_settings, "Batching", "min_batch")
            .value_or(1),
        getSettings<int>(json_settings, "Batching", "max_batch")
            .value_or(2000),
        getSettings<int>(json_settings, "Batching", "min_batch_dist")
            .value_or(1),
        getSettings<int>(json_settings, "Batching", "max_batch_dist")
            .value_or(8000),
        getSettings<bool>(json_settings, "Batching", "enable_memory_cap")
            .value_or(true)
    );
  }
  

  hru_actor_settings_ = HRUActorSettings(
    getSettings<bool>(json_settings, "HRU_Actor", "print_output")
        .value_or(true),
    getSettings<int>(json_settings, "HRU_Actor", "output_frequency")
        .value_or(OUTPUT_FREQUENCY));


  return SUCCESS;
}



std::optional<std::vector<std::string>> Settings::getSettingsArray(
		json settings, std::string key_1, std::string key_2) {
  std::vector<std::string> return_vector;
  // find first key
  try {
    if (settings.find(key_1) != settings.end()) {
      json key_1_settings = settings[key_1];

      // find value behind second key
      if (key_1_settings.find(key_2) != key_1_settings.end()) {
        for(auto& host : key_1_settings[key_2])
          return_vector.push_back(host["hostname"]);
      
        return return_vector;
      } 
      else 
        return {};

    } 
    else
      return {}; // return none in the optional (error value)
  } catch (json::exception& e) {
    std::cout << e.what() << "\n";
    std::cout << key_1 << "\n";
    std::cout << key_2 << "\n";
    return {};
  }
}


void Settings::printSettings() {
  std::cout << "************ BATCHING SETTINGS **************\n"
            << "Batching settings present: " << batching_settings_present_ << "\n"
            << batch_settings_.toString() << "\n"
            << "************ DISTRIBUTED_SETTINGS ************\n"
            << distributed_settings_.toString() << "\n"
            << "************ SUMMA_ACTORS SETTINGS ************\n"
            << summa_actor_settings_.toString() << "\n"
            << "************ FILE_ACCESS_ACTOR SETTINGS ************\n"
            << fa_actor_settings_.toString() << "\n"
            << "************ JOB_ACTOR SETTINGS ************\n"
            << job_actor_settings_.toString() << "\n"
            << "************ HRU_ACTOR SETTINGS ************\n"
            << hru_actor_settings_.toString() << "\n"
            << "********************************************\n\n";
}

void Settings::generateConfigFile() {
    using json = nlohmann::ordered_json;
    json config_file; 
    config_file["Summa_Actor"] = {
        {"max_gru_per_job", GRU_PER_JOB},
        {"enable_logging", false},
        {"log_dir", ""}
    };
    config_file["File_Access_Actor"] = {
        {"num_partitions_in_output_buffer", NUM_PARTITIONS},
        {"num_timesteps_in_output_buffer", OUTPUT_TIMESTEPS}
    };
    config_file["Job_Actor"] = {
        {"file_manager_path", "/home/username/summa_file_manager"},
        {"max_run_attempts", 1},
        {"data_assimilation_mode", false},
        {"batch_size", MISSING_INT}
    };
    config_file["HRU_Actor"] = {
        {"print_output", true},
        {"output_frequency", OUTPUT_FREQUENCY},
        {"abs_tol", 1e1},
        {"rel_tol", 1e1},
        {"rel_tol_temp_cas", 1e1},
        {"rel_tol_temp_veg", 1e1},
        {"rel_tol_wat_veg", 1e1},
        {"rel_tol_temp_soil_snow", 1e1},
        {"rel_tol_wat_snow", 1e1},
        {"rel_tol_matric", 1e1},
        {"rel_tol_aquifr", 1e1},
        {"abs_tol_temp_cas", 1e1},
        {"abs_tol_temp_veg", 1e1},
        {"abs_tol_wat_veg", 1e1},
        {"abs_tol_temp_soil_snow", 1e1},
        {"abs_tol_wat_snow", 1e1},
        {"abs_tol_matric", 1e1},
        {"abs_tol_aquifr", 1e1},
        {"default_tol", true}
    };

    std::ofstream config_file_stream("config.json");
    config_file_stream << std::setw(4) << config_file.dump(2) << std::endl;
    config_file_stream.close();
}

// This function determines the effective batch size based on the settings and total work units
int Settings::getEffectiveBatchSize(int total_units) const {
  // Defensive
  if (total_units <= 0) return 1;

  // If Batching exists in config, it overrides BOTH legacy fields
  if (batching_settings_present_) {
    const bool dist = distributed_settings_.distributed_mode_;

    // detect cores
    unsigned cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 1;

    int chosen = 1;

    // -----------------------
    // FIXED mode
    // -----------------------
    if (batch_settings_.mode_ == BATCH_FIXED) {
      chosen = batch_settings_.fixed_batch_size_;

      // If fixed_batch_size missing/invalid, fall back safely
      if (chosen <= 0 || chosen == MISSING_INT) {
        chosen = static_cast<int>(cores); // very safe fallback
      }
    }
    // -----------------------
    // ADAPTIVE (Approach 1 or 2)
    // -----------------------
    else {
      // Approach 1 baseline: batch_cpu = α*cores (single) or β*cores (dist)
      int scale = dist ? batch_settings_.beta_ : batch_settings_.alpha_;
      long long batch_cpu = static_cast<long long>(scale) * static_cast<long long>(cores);

      // Apply bounds (different bounds for dist vs single)
      if (dist) {
        if (batch_cpu < batch_settings_.min_batch_dist_) batch_cpu = batch_settings_.min_batch_dist_;
        if (batch_cpu > batch_settings_.max_batch_dist_) batch_cpu = batch_settings_.max_batch_dist_;
      } else {
        if (batch_cpu < batch_settings_.min_batch_) batch_cpu = batch_settings_.min_batch_;
        if (batch_cpu > batch_settings_.max_batch_) batch_cpu = batch_settings_.max_batch_;
      }

      chosen = static_cast<int>(batch_cpu);

      // -----------------------
      // Approach 2: Memory-capped heuristic
      // Only apply if mode is ADAPTIVE_CPU_MEM (and memory cap enabled)
      // batch = min(total_units, batch_cpu, batch_mem)
      // -----------------------
      if (batch_settings_.mode_ == BATCH_ADAPTIVE_CPU_MEM &&
          batch_settings_.enable_memory_cap_) {
        int ram_gb = getTotalRamGB();
        int batch_mem = memoryCapFromRamGB(ram_gb);
        chosen = std::min(chosen, batch_mem);
      }
    }

    // Always cap by workload
    chosen = std::min(chosen, total_units);

    // Final safety clamp
    if (chosen < 1) chosen = 1;

    return chosen;
  }

  // -----------------------
  // Legacy behavior if "Batching" is not present
  // -----------------------
  if (distributed_settings_.distributed_mode_) {
    int legacy = distributed_settings_.num_hru_per_batch_;
    if (legacy <= 0) legacy = 1;
    return std::min(legacy, total_units);
  }

  int legacy = job_actor_settings_.batch_size_;
  if (legacy <= 0 || legacy == MISSING_INT) legacy = 1;
  return std::min(legacy, total_units);
}

void Settings::applyEffectiveBatchSize(int total_units) {
  int eff = getEffectiveBatchSize(total_units);

  // Make it affect BOTH code paths, regardless of which field they read
  job_actor_settings_.batch_size_ = eff;
  distributed_settings_.num_hru_per_batch_ = eff;

  std::cout << "[Batching] effective_batch_size=" << eff
            << " total_units=" << total_units
            << " distributed_mode=" << distributed_settings_.distributed_mode_
            << " cores=" << std::thread::hardware_concurrency()
            << "\n";
}