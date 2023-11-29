#include "dram_system.h"
#include <assert.h>

namespace dramsim3 {

// alternative way is to assign the id in constructor but this is less
// destructive
int BaseDRAMSystem::total_channels_ = 0;

BaseDRAMSystem::BaseDRAMSystem(Config &config, const std::string &output_dir,
                               std::function<void(uint64_t)> read_callback,
                               std::function<void(uint64_t)> write_callback)
    : read_callback_(read_callback),
      write_callback_(write_callback),
      last_req_clk_(0),
      config_(config),
      timing_(config_),
#ifdef THERMAL
      thermal_calc_(config_),
#endif  // THERMAL
      clk_(0) {
    total_channels_ += config_.channels;

    std::cout<<config_.ranks<<config_.banks_per_group<<config_.bankgroups<<std::endl;
#ifdef ADDR_TRACE
    std::string addr_trace_name = config_.output_prefix + "addr.trace";
    address_trace_.open(addr_trace_name);
#endif
}

int BaseDRAMSystem::GetChannel(uint64_t hex_addr) const {
    hex_addr >>= config_.shift_bits;
    return (hex_addr >> config_.ch_pos) & config_.ch_mask;
}

void BaseDRAMSystem::PrintEpochStats() {
    // first epoch, print bracket
    if (clk_ - config_.epoch_period == 0) {
        std::ofstream epoch_out(config_.json_epoch_name, std::ofstream::out);
        epoch_out << "[";
    }
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->PrintEpochStats();
        std::ofstream epoch_out(config_.json_epoch_name, std::ofstream::app);
        epoch_out << "," << std::endl;
    }
#ifdef THERMAL
    thermal_calc_.PrintTransPT(clk_);
#endif  // THERMAL
    return;
}

void BaseDRAMSystem::PrintStats() {
    // Finish epoch output, remove last comma and append ]
    std::ofstream epoch_out(config_.json_epoch_name, std::ios_base::in |
                                                         std::ios_base::out |
                                                         std::ios_base::ate);
    epoch_out.seekp(-2, std::ios_base::cur);
    epoch_out.write("]", 1);
    epoch_out.close();

    std::ofstream json_out(config_.json_stats_name, std::ofstream::out);
    json_out << "{";

    // close it now so that each channel can handle it
    json_out.close();
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->PrintFinalStats();
        if (i != ctrls_.size() - 1) {
            std::ofstream chan_out(config_.json_stats_name, std::ofstream::app);
            chan_out << "," << std::endl;
        }
    }
    json_out.open(config_.json_stats_name, std::ofstream::app);
    json_out << "}";

#ifdef THERMAL
    thermal_calc_.PrintFinalPT(clk_);
#endif  // THERMAL
}

void BaseDRAMSystem::ResetStats() {
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->ResetStats();
    }
}

void BaseDRAMSystem::RegisterCallbacks(
    std::function<void(uint64_t)> read_callback,
    std::function<void(uint64_t)> write_callback) {
    // TODO this should be propagated to controllers
    read_callback_ = read_callback;
    write_callback_ = write_callback;
}

JedecDRAMSystem::JedecDRAMSystem(Config &config, const std::string &output_dir,
                                 std::function<void(uint64_t)> read_callback,
                                 std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config, output_dir, read_callback, write_callback) {
    if (config_.IsHMC()) {
        std::cerr << "Initialized a memory system with an HMC config file!"
                  << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    ctrls_.reserve(config_.channels);
    for (auto i = 0; i < config_.channels; i++) {
#ifdef THERMAL
        ctrls_.push_back(new Controller(i, config_, timing_, thermal_calc_));
#else
        ctrls_.push_back(new Controller(i, config_, timing_));
#endif  // THERMAL
    }

    int banks =  config_.ranks * config_.bankgroups * config_.banks_per_group;
    std::cout<<"rank: "<<config_.ranks<<" bgs: "<<config_.bankgroups<<" bpg: "<<config_.banks_per_group<<std::endl;
    for (auto i = 0; i < config_.channels; i++) {
        auto chan_occupancy =
            std::vector<bool>(banks, false);
        bank_occupancy_.push_back(chan_occupancy);
    }
}

JedecDRAMSystem::~JedecDRAMSystem() {
    for (auto it = ctrls_.begin(); it != ctrls_.end(); it++) {
        delete (*it);
    }
}


bool JedecDRAMSystem::WillAcceptTransaction() const {
    return pim_trans_queue_.size() < pim_trans_queue_depth_;
}

bool JedecDRAMSystem::AddTransaction(uint64_t hex_addr) {
// Record trace - Record address trace for debugging or other purposes
#ifdef ADDR_TRACE
    address_trace_ << std::hex << hex_addr << std::dec << " "
                   << "PIM " << clk_ << std::endl;
#endif

    bool ok = WillAcceptTransaction();

    assert(ok);
    if (ok) {
        Transaction trans = Transaction(hex_addr);
        pim_trans_queue_.push_back(trans);

    }
    last_req_clk_ = clk_;
    return ok;
}

bool JedecDRAMSystem::WillAcceptTransaction(uint64_t hex_addr,
                                            bool is_write) const {
    int channel = GetChannel(hex_addr);
    return ctrls_[channel]->WillAcceptTransaction(hex_addr, is_write);
}

bool JedecDRAMSystem::AddTransaction(uint64_t hex_addr, bool is_write) {
// Record trace - Record address trace for debugging or other purposes
#ifdef ADDR_TRACE
    address_trace_ << std::hex << hex_addr << std::dec << " "
                   << (is_write ? "WRITE " : "READ ") << clk_ << std::endl;
#endif

    int channel = GetChannel(hex_addr);
    bool ok = ctrls_[channel]->WillAcceptTransaction(hex_addr, is_write);

    assert(ok);
    if (ok) {
        Transaction trans = Transaction(hex_addr, is_write);
        ctrls_[channel]->AddTransaction(trans);
    }
    last_req_clk_ = clk_;
    return ok;
}

void JedecDRAMSystem::ClockTick() {
    for (size_t i = 0; i < ctrls_.size(); i++) {
        // look ahead and return earlier
        while (true) {
            auto pair = ctrls_[i]->ReturnDoneTrans(clk_);
            if (pair.second == 1) {
                write_callback_(pair.first);
            } else if (pair.second == 0) {
                read_callback_(pair.first);
            } else {
                break;
            }
        }
    }

    // std::cout<<"Clock Cycle "<<clk_<<std::endl;

    //lookup refresh_ in each controller to check refresh countdown
    //if countdown is lower than pim delay, pause issuing pim commands until refresh is done over all ranks
    bool wait_refresh = false;
    for (size_t i=0; i<ctrls_.size(); i++) {
        if (ctrls_[i]->pim_refresh_coming() && vcuts != -1 && hcuts != -1) {
            wait_refresh = true;
            for (int j=0; j<vcuts*hcuts; j++) {
                in_act_placed[j] = false;
                w_act_placed[j] = false;
                out_act_placed[j] = false;
            }
            // std::cout<<clk_ << "\tWait Refresh\n";
        }
    }

    //TODO npu signals
    bool weight_fetching = false;
    bool input_sending = false;
    int tensor_bitwidth = 3;
    bool input_trans_found = false;
    bool weight_trans_found = false;
    bool output_trans_found = false;



    if (!pim_trans_queue_.empty()) { // wait_refresh: when sending an activation cmd. no configured var
        int cut_no;
        int bw_cutNo = 4;
        int bw_vcuts = 3;
        int bw_hcuts = 1;
        int bw_mcf = 3;
        int bw_ucf = 3;
        int bw_df = 1;
        int bw_Mtile = 4;
        int bw_kernelSize = 5;
        int bw_stride = 5;
        int bw_dimValue = 32;
        int bw_baseRow = 22;
        int bw_loadType = 2;

        auto it = pim_trans_queue_.begin();
        uint64_t address = it->addr;

        if (it->addr & 1) { // computing
            address = address >> 1;
            int cuts = vcuts * hcuts;
            bool configured = true;
            for (int i=0; i<cuts; i++)
                if ((address & (1 << i)) && (M[i] != 0 && N[i] != 0 && K[i] != 0));
                else {
                    configured = false;
                }
            if (configured) {
                for (int i=0; i<cuts; i++)
                    if(address & (1 << i))
                        in_pim[i] = true;
                pim_trans_queue_.erase(it);
            }
        }
        else if ((it->addr & (1 << 6)) && (it->addr & (1 << 5))) { // cutting


            base_rows_w.clear();
            base_rows_in.clear();
            base_rows_out.clear();
            M.clear();
            N.clear();
            K.clear();
            M_it.clear();
            K_tile_it.clear();
            N_it.clear();
            M_out_it.clear();
            N_out_tile_it.clear();
            in_pim.clear();
            iw_status.clear();
            in_cnt.clear();
            out_cnt.clear();
            vpu_cnt.clear();
            in_act_placed.clear();
            w_act_placed.clear();
            out_act_placed.clear();
            output_valid.clear();

            address = address >> 1 >> 4 >> 2; // trans_type, cut_no, loadType
            vcuts = 1 << (address & ((1<<bw_vcuts)-1));
            address = address >> bw_vcuts;
            hcuts = 1 << (address & ((1<<bw_hcuts)-1));
            address = address >> bw_hcuts;
            mcf = 1 << (address & ((1<<bw_mcf)-1));
            address = address >> bw_mcf;
            ucf = 1 << (address & ((1<<bw_ucf)-1));
            address = address >> bw_ucf;
            df = address & ((1<<bw_df)-1);
            address = address >> bw_df;


            mc = mcf * ucf;
            if (vcuts * hcuts > 1) // TODO
                for (int i=0; i<ctrls_.size(); i++) {
                    ctrls_[i]->wr_multitenant = true;
                }

            M_tile_size = 1 << (address & ((1<<bw_Mtile)-1));
            address = address >> bw_Mtile;
            vcuts_next = 1 << (address & ((1<<bw_vcuts)-1));
            address = address >> bw_vcuts;
            hcuts_next = 1 << (address & ((1<<bw_hcuts)-1));
            address = address >> bw_hcuts;
            kernel_size = address & ((1<<bw_kernelSize)-1);
            address = address >> bw_kernelSize;
            stride = address & ((1<<bw_stride)-1);

            assert(M_tile_size <= 2048); // TODO accurate value

            int cuts = vcuts * hcuts;
            base_rows_w.assign(cuts, 0);
            base_rows_in.assign(cuts, 0);
            base_rows_out.assign(cuts, 0);
            M.assign(cuts, 0);
            N.assign(cuts, 0);
            K.assign(cuts, 0);
            M_it.assign(cuts, 0);
            K_tile_it.assign(cuts, 0);
            N_it.assign(cuts, 0);
            M_out_it.assign(cuts, 0);
            N_out_tile_it.assign(cuts, 0);
            in_pim.assign(cuts, false);
            iw_status.assign(cuts, 0);
            in_cnt.assign(cuts, 0);
            out_cnt.assign(cuts, -1);
            vpu_cnt.assign(cuts, 0);
            in_act_placed.assign(cuts, false);
            w_act_placed.assign(cuts, false);
            out_act_placed.assign(cuts, false);
            output_valid.assign(cuts, 0);

            pim_trans_queue_.erase(it);
        }
        else { // loading
            address = address >> 1;
            cut_no = address & ((1 << bw_cutNo)-1);
            address = address >> 4;
            int loadType = address & ((1<<bw_loadType)-1);
            address = address >> bw_loadType;
            int dim_value = address & (((uint64_t)1<<bw_dimValue)-1);
            address = address >> bw_dimValue;
            uint64_t base_row = address & ((1<<bw_baseRow)-1);
            address = address >> bw_baseRow;
            switch(loadType) {
                case 0: // M, weight
                    base_rows_w[cut_no] = base_row;
                    M[cut_no] = dim_value;
                    break;
                case 1: // K, output
                    base_rows_out[cut_no] = base_row;
                    // std::cout<<base_row<<std::endl;
                    K[cut_no] = dim_value;
                    break;
                case 2: // N, input
                    base_rows_in[cut_no] = base_row;
                    N[cut_no] = dim_value;
                    break;
                default:
                    std::cerr << "Invalid load type!"
                              << std::endl;
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            pim_trans_queue_.erase(it);

        }
    }

    bool is_in_ref = false;
    for (int i=0; i<ctrls_.size(); i++) {
        if (ctrls_[i]->IsInRef() || ctrls_[i]->pim_refresh_coming2())
            is_in_ref = true;
    }

    int cuts = 0;
    if (vcuts != -1 && hcuts != -1) cuts = vcuts * hcuts;
    for (int i=0; i < cuts; i++) {
        if (!in_pim[i] || is_in_ref) continue;

        int vcut_no = i % vcuts;
        int cut_height = config_.channels / hcuts;
        int hcut_no = i / vcuts;
        int cut_width = config_.banks / vcuts;

        int N_tile_size = 128 / vcuts; // TODO 128 : the number of PEs in a row
        int N_tile_it = N_it[i] / N_tile_size;
        int M_tile_it = M_it[i] / M_tile_size;
        int M_current_tile_size = M[i] < M_tile_size * (M_tile_it + 1) ? M[i] % M_tile_size : M_tile_size;
        int K_tile_size = std::min(cut_height * 16, K[i]); // TODO 16: the number of PEs supported by a bank's io


        // TODO
        int weight_banks_reduce = df == 1 ? 16 : 1;
        // TODO make vector of pairs
        // std::vector<std::pair<Command, int>> iw_cmds
        // auto cmd_pair = std::make_pair(cmd, i);
        std::vector<std::vector<Command>> in_cmds(cuts);
        std::vector<std::vector<Command>> w_cmds(cuts);

        bool output_ready = iw_status[i] == 3; // : iw_status[i] == 2 || in_cnt[i] == -1;

        // std::cout<<iw_status[i]<<"iw_status\n";
        switch (iw_status[i]) {
            case 0: { // Fetching weight
                /*
                if (mc == 16) {
                    iw_status[i]++;
                    break;
                }*/
                CommandType act_type = CommandType::PIM_ACTIVATE;
                CommandType read_type = CommandType::PIM_READ;
                CommandType readp_type = CommandType::PIM_READ_PRECHARGE;


                int N_tile_size_per_bank = std::min(N[i], (N_tile_size-1)/(cut_width/weight_banks_reduce) + 1);
                int col_offset = N_tile_it * (N_tile_size_per_bank * ((K[i]-1) / K_tile_size + 1)) + K_tile_it[i] * N_tile_size_per_bank + N_it[i] % N_tile_size; // N_it incremented by N_tile_size when N_it % N_tile_size_per_bank == 0 (but not with N_tile_size)
                for (int j=0; j<cut_height; j++) {
                    for (int k=0; k<cut_width/weight_banks_reduce; k++) {
                        // TODO offset must be divided by row_width/dev_width
                        int ch = hcut_no * cut_height + j;
                        int bk = vcut_no * cut_width + k * weight_banks_reduce;
                        int bg = bk / config_.banks_per_group;
                        bk = bk % config_.banks_per_group;
                        Address addr = Address(ch, 0, bg, bk, base_rows_w[i] + col_offset/(config_.columns/config_.BL),  col_offset % (config_.columns/config_.BL));
                        uint64_t hex_addr = config_.AddressUnmapping(addr);
                        CommandType cmd_type;
//                        if (mc == 16)
//                            cmd_type =  (addr.column + 1) % ucf == 0 ? CommandType::PIM_READ_PRECHARGE : CommandType::PIM_READ;
//                        else
                            cmd_type = (addr.column + 1) % (std::min(N[i], ((128 / config_.banks) * weight_banks_reduce))) == 0 || (addr.column + 1) % (config_.columns / config_.BL) == 0   ? readp_type : read_type; // TODO
                        Command cmd = Command(cmd_type, addr, hex_addr);
                        Command ready_cmd = ctrls_[ch]->GetReadyCommand(cmd, clk_);
                        if (!ready_cmd.IsValid()) {
                            // std::cout<<clk_<<" Invalid Command: "<<ready_cmd<<std::endl;
                            w_cmds[i].clear();
                            break;
                        }
                        else {
                            w_cmds[i].push_back(ready_cmd);
                            if (w_cmds[i].begin()->cmd_type != ready_cmd.cmd_type) {
                                // std::cout<<clk_<<" Unmatched Commands\n"<<ready_cmd<<std::endl<<*(w_cmds[i].begin())<<std::endl; //TODO push back when it is not read or write?
                                w_cmds[i].clear();
                                break;
                            }
                        }
                    }
                    if (w_cmds[i].empty()) break;
                }


                if (w_cmds[i].empty()) break;

                if (w_cmds[i].begin()->cmd_type == act_type) {
                    if (w_act_placed[i] || wait_refresh) {
                        w_cmds[i].clear();
                        break;
                    }
                    else
                        w_act_placed[i] = true;
                }
                else {
                    if (w_cmds[i].begin()->cmd_type == readp_type) {
                        w_act_placed[i] = false;
                    }
                    if (df == 1 && w_cmds[i].begin()->cmd_type == CommandType::PRECHARGE) {
                        break;
                    }
                    // std::cout<<"Fetch Weight\n";

                    // increment iterators
                    N_it[i]++;
                    if (N_it[i] % N_tile_size_per_bank == 0 && (N_tile_size == N_tile_size_per_bank || N_it[i] % N_tile_size != 0)) {
                        N_it[i] = N_tile_size * N_tile_it;
                        iw_status[i]++;
                    }
                }


                break;
            }
            case 1: { // Finished weight
                // wait npu_signals
                iw_status[i]++;
                vpu_cnt[i] = 1; //8*weight_banks_reduce;
                if (cuts == 1) { //N[i]==1) { //TODO Multi-Tenant
                    for (int j=0; j<iw_status.size(); j++) {
                        if (iw_status[j] == 0 || iw_status[j] == 3) {
                            iw_status[i]--;
                            break;
                        }
                    }
                }
                break;
            }
            case 2: { // Feeding input
                vpu_cnt[i]--;
                vpu_cnt[i] = std::max(0, vpu_cnt[i]);

                int col_offset = M_tile_it * (M_tile_size * ((K[i]-1) / K_tile_size + 1)) + K_tile_it[i] * M_current_tile_size + M_it[i] % M_tile_size;
                bool mixed = false;
                for (int j=0; j<cut_height; j++) {
                    for (int k=0; k<mc; k++) {
                        // TODO offset must be divided by row_width/dev_width
                        int ch = hcut_no * cut_height + j;
                        int bk = vcut_no * cut_width + k*(cut_width/mc);
                        int bg = bk / config_.banks_per_group;
                        bk = bk % config_.banks_per_group;
                        Address addr = Address(ch, 0, bg, bk, base_rows_in[i] + col_offset/(config_.columns/config_.BL),  col_offset % (config_.columns/config_.BL));
                        uint64_t hex_addr = config_.AddressUnmapping(addr);
                        bool close = M_it[i] + 1 == M[i]; // prevent closing between tiles
                        close = df == 1 ? close && (K_tile_it[i]+1) * K_tile_size >= K[i] : close;
                        CommandType cmd_type = addr.column == config_.columns / config_.BL - 1  || close ? CommandType::PIM_READ_PRECHARGE : CommandType::PIM_READ;
                        Command cmd = Command(cmd_type, addr, hex_addr);
                        Command ready_cmd = ctrls_[ch]->GetReadyCommand(cmd, clk_);
                        if (!ready_cmd.IsValid()) {
                            in_cmds[i].clear();
                            break;
                        }
                        else {
                            in_cmds[i].push_back(ready_cmd);
                            if (in_cmds[i].begin()->cmd_type != ready_cmd.cmd_type) {
                                mixed = true;

                            }
                        }
                    }
                }
                if(cuts > 1 && in_cmds[i].size() != cut_height) {
                    in_cmds[i].clear();
                    break;
                }
                if (mixed) {
                    for (auto it = in_cmds[i].begin(); it != in_cmds[i].end();) {
                        if (it->cmd_type == CommandType::PIM_READ || it->cmd_type == CommandType::PIM_READ_PRECHARGE) {
                            it = in_cmds[i].erase(it);
                        }
                        else
                            it++;
                    }
                }

                if (in_cmds[i].empty()) break;

                if (in_cmds[i].begin()->cmd_type == CommandType::PIM_ACTIVATE) {
                    if ((!mixed && in_act_placed[i]) || wait_refresh) {
                        in_cmds[i].clear();
                        break;
                    }
                    else
                        in_act_placed[i] = true;
                }
                else {

                    if (in_cmds[i].begin()->cmd_type == CommandType::PIM_READ_PRECHARGE) {
                        in_act_placed[i] = false;
                    }
                    if (vpu_cnt[i]!=0){
                        in_cmds[i].clear();
                        break;
                    }
                    // std::cout<<"Feed Input\n";

                    assert(M_tile_size > 128/vcuts);

                    if ((K_tile_it[i]+1) * K_tile_size >= K[i] && M_it[i] % M_tile_size == 0) { // TODO
                        out_cnt[i] = std::max(1, config_.tCCD_L * (3 + 16) - config_.tRCDWR); // TODO log(8) + 128 / 8  + extra
                    }

                    M_it[i]++;
                    // if (i==3) for (auto it = in_cmds[i].begin(); it != in_cmds[i].end(); it++) std::cout<<clk_<<mixed<<*it<<" N"<<N_it[i]<<" M"<<M_it[i]<<" MT"<<M_tile_size<<" K"<<K_tile_it[i]<<std::endl;
                    if (M_it[i] % M_tile_size == 0 || M_it[i] == M[i]) { // TODO
                        in_cnt[i] = std::max(1, config_.tCCD_L * std::max(128/(vcuts*mc), 16) - config_.tRCDRD); // (vcuts*mc) TODO subtract tRP+tRCD
                        iw_status[i]++;
                        M_it[i] = M_tile_size * M_tile_it;
                        K_tile_it[i]++;

                        if (K_tile_it[i] * K_tile_size >= K[i]) {
                            // out_cnt[i] = 3;
                            K_tile_it[i] = 0;
                            N_it[i] = N_tile_size * (N_tile_it+1);
                            if (N_it[i] >= N[i]) {
                                N_it[i] = 0;
                                M_it[i] = M_tile_size * (M_tile_it + 1);
                                if (M_it[i] >= M[i]) {
                                    std::cout<<clk_<<" End of Computation "<<i<<std::endl;
                                    in_cnt[i] = -1;
                                }
                            }
                        }
                    }
                }
                break;
            }
            case 3: {// Finished input
                if (in_cnt[i] == -1) break;
                else {

                    in_cnt[i] = std::max(0, in_cnt[i] - 1);
                    if (in_cnt[i] == 0 && output_valid[i] == 0)
                        iw_status[i] = 0;
                    break;
                }
                /*
                else {


                    in_cnt[i] = std::max(0, in_cnt[i] - 1);
                    if (in_cnt[i] != 0) break;
                    bool all_closed = true;
                    for (int j=0; j<cut_height; j++) {
                        for (int k=0; k<cut_width/weight_banks_reduce; k++) {
                            int ch = hcut_no * cut_height + j;
                            int bk = vcut_no * cut_width + k * weight_banks_reduce;
                            int bg = bk / config_.banks_per_group;
                            bk = bk % config_.banks_per_group;
                            Address addr = Address(ch, 0, bg, bk, 0, 0);
                            uint64_t hex_addr = config_.AddressUnmapping(addr);
                            Command cmd = Command(CommandType::PIM_READ, addr, hex_addr);
                            Command ready_cmd = ctrls_[ch]->GetReadyCommand(cmd, clk_);
                            switch (ready_cmd.cmd_type) {
                                case CommandType::PIM_READ:
                                    ready_cmd = Command(CommandType::PRECHARGE, addr, hex_addr);
                                case CommandType::PRECHARGE:
                                    w_cmds[i].push_back(ready_cmd);
                                    // std::cout<<clk_<<"\t"<<ready_cmd<<std::endl;
                                    all_closed = false;
                                    break;

                                case CommandType::SIZE:
                                    all_closed = false;
                                    break;

                                case CommandType::PIM_ACTIVATE:
                                    break;

                                default:
                                    AbruptExit(__FILE__, __LINE__);
                                    break;
                            }
                        }
                    }
                    if (all_closed) { //TODO Multi-Tenant
                        iw_status[i] = 0;
                        if (cuts == 1) { //N[i]==1) {
                            for (int j=0; j<iw_status.size(); j++) {
                                if (iw_status[j] == 2) {
                                    iw_status[i] = 3;
                                    break;
                                }
                            }
                        }
                        if (iw_status[i] == 0 && out_act_placed[i]) out_act_placed[i] = false;
                    }


                }*/
                break;
            }
            default: {
                break;
            }
        }

        // TODO simulate npu_signal


        if (out_cnt[i] == 0) output_valid[i]++;
        if (out_cnt[i] != -1) out_cnt[i]--;


        std::vector<std::vector<Command>> out_cmds(cuts);


        bool out_enable = cut_height / vcuts > 0 || vcut_no % 2 == 0;
        if (output_valid[i] > 0 && output_ready && out_enable) {
            int vcut_out_no = M[i] == 1 ? vcut_no : vcuts == 16 ? vcut_no / 2 : (vcut_no + N_out_tile_it[i]) % vcuts; // relates to channel number
            int M_tile_size_out = df == 1 ? (M_tile_size/128)*mcf : M_tile_size;
            int M_out_tile_it = M_out_it[i] / M_tile_size_out;
            int M_out = df == 1 ? std::max(1, M[i]*mcf / 128) : M[i];
            int M_out_current_tile_size = M_out < M_tile_size_out * (M_out_tile_it + 1) ? M_out % M_tile_size_out : M_tile_size_out;
            int N_out = df == 1 ? 128 : N[i];
            int N_tile_size_out = df == 1 ? 128 : N_tile_size;
            int N_tile_num = (N[i]-1) / N_tile_size_out + 1;
            int N_tile_num_ch = (N_tile_num) / vcuts; // varies by channels to be accessed
            N_tile_num_ch += N_tile_num % vcuts > N_out_tile_it[i] % vcuts ? 1 : 0;
            int N_tile_it_ch = N_out_tile_it[i] / vcuts;
            int col_offset = M_out_tile_it * (M_tile_size_out * N_tile_num_ch) + N_tile_it_ch * M_out_current_tile_size + M_out_it[i] % M_tile_size_out;

            int cut_height_out = cut_height < vcuts ? 1 : cut_height / vcuts;
            for (int j=0; j<cut_height_out; j++) {
                // TODO offset must be divided by row_width/dev_width
                int ch = hcut_no * cut_height + vcut_out_no * cut_height_out + j;
                int k_bound = df == 1 ? 1 : M[i] == 1 || true ? mc : 1; // TODO M[i]==1? systolic effect
                for (int k=0; k<k_bound; k++) {
                    int bk = vcut_no * cut_width + k*(cut_width/mc);
                    if (df != 1) bk++;
                    int bg = bk / config_.banks_per_group;
                    bk = bk % config_.banks_per_group;
                    Address addr = Address(ch, 0, bg, bk, base_rows_out[i] + col_offset/(config_.columns/config_.BL),  col_offset % (config_.columns/config_.BL));
                    // Address addr = Address(ch, 0, bg, bk, base_rows_out[i] + col_offset,  col_offset % (config_.columns/config_.BL));
                    uint64_t hex_addr = config_.AddressUnmapping(addr);
                    CommandType cmd_type = addr.column == config_.columns / config_.BL - 1 || M_out_it[i] + 1 == M_out ? CommandType::PIM_WRITE_PRECHARGE : CommandType::PIM_WRITE;
                    Command cmd = Command(cmd_type, addr, hex_addr);
                    Command ready_cmd = ctrls_[ch]->GetReadyCommand(cmd, clk_);


                    if (!ready_cmd.IsValid()) {
                        out_cmds[i].clear();
                        break;
                    }
                    else {
                        out_cmds[i].push_back(ready_cmd);
                        if (out_cmds[i].begin()->cmd_type != ready_cmd.cmd_type) {
                            out_cmds[i].clear();
                            break;
                        }
                    }
                }
                if (out_cmds[i].empty()) break;
            }

            if (!out_cmds[i].empty()) {
                if (out_cmds[i].begin()->cmd_type == CommandType::PIM_ACTIVATE) {
                    if (out_act_placed[i] || wait_refresh) {
                        out_cmds[i].clear();
                    }
                    else {
                        out_act_placed[i] = true;
                    }
                }
                else {
                    if (out_cmds[i].begin()->cmd_type == CommandType::PIM_WRITE_PRECHARGE) {
                        out_act_placed[i] = false;
                    }
                // std::cout<<"Write Output\n";

                    M_out_it[i]++;
                    //std::cout<<clk_<<" "<<i<<" Mo"<<M_out_it[i]<<" No"<<N_out_tile_it[i]<<std::endl;
                    if (M_out_it[i] % M_tile_size_out == 0 || M_out_it[i] == M_out) {
                        M_out_it[i] = M_tile_size_out * M_out_tile_it;
                        N_out_tile_it[i]++;
                        if (N_out_tile_it[i] * N_tile_size_out >= N_out) {
                            N_out_tile_it[i] = 0;
                            M_out_it[i] = M_tile_size_out * (M_out_tile_it+1);
                            if (M_out_it[i] >= M_out) {
                                assert(in_cnt[i] == -1);
                                std::cout<<clk_<<" Output Exhausted. Array"<<i<<" Turn off PIM mode.\n";
                                in_pim[i] = false;
                                if (cut_height < vcuts) in_pim[i+1] = false;
                                turn_off = true;
                                for (size_t j = 0; j < in_pim.size(); j++) {
                                    if (in_pim[j]) {
                                        turn_off = false;
                                        std::cout<<j<< " " << M_out_it[j]<<" "<<K_tile_it[j]*K_tile_size<<" "<<N_out_tile_it[j]*N_tile_size_out<<std::endl;
                                    }

                                }
                            }

                        }

                        output_valid[i]--;
                        if (cut_height < vcuts) output_valid[i+1]--;
                        // Output Tile Finished
                    }
                }
            }

        }
        for (auto& it: w_cmds) {
            for (auto& it2: it) {
               // std::cout<<clk_<<" "<<it<<std::endl;
                ctrls_[it2.Channel()]->rd_w_cmds_.push_back(it2);
            }
        }
        for (auto& it: in_cmds) {
            for (auto& it2: it) {
                ctrls_[it2.Channel()]->rd_in_cmds_.push_back(it2);
                int release_time_ = clk_;
                if (it2.cmd_type == CommandType::PIM_ACTIVATE) release_time_ += 0;  // + (it.Channel() % cut_height)*config_.tCCD_S); //TODO
                ctrls_[it2.Channel()]->release_time.push_back(release_time_);
            }
        }
        for (auto& it: out_cmds) {
            for (auto& it2: it) {

                ctrls_[it2.Channel()]->wr_cmds_.push_back(it2);
            }
        }

    }




    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->ClockTick();
    }

    clk_++;

    if (clk_ % config_.epoch_period == 0) {
        PrintEpochStats();
    }
    return;
}

Command JedecDRAMSystem::GetReadyCommandPIM(Transaction trans, CommandType type) {
    bool first = true;
    bool sameornot = false;
    Command ready_cmd = Command();
    if (trans.targetChans.empty() || trans.targetBanks.empty()) std::cout<<"empty target Chans, Banks\t"<<trans<<'\n';
    for (auto& itC : trans.targetChans) {
        // We do not need to add more loops here since only these two dimensions are orthogonal to each other
        for (auto& itB : trans.targetBanks) {
            // TODO how to set rank, bankgroup, and column address (device width)?
            Address addr = Address((int) itC, 0, 0, (int) itB, (int) trans.row_addr,  (int) trans.col_num);
            Command cmd = Command(type, addr, trans.addr);
            std::cout<<clk_<<"\tWant To issue: " << cmd << '\n';
            ready_cmd = ctrls_[itC]->GetReadyCommand(cmd, clk_);
            if (first) {
                first = false;
                sameornot = cmd.cmd_type == ready_cmd.cmd_type;
            }
            else {
                if (sameornot != (cmd.cmd_type == ready_cmd.cmd_type)) {
                    std::cout <<clk_<< "\tSame or not fail:\n" << cmd << '\n' << ready_cmd << '\n';
                    return Command();
                }
            }
            if (!ready_cmd.IsValid() || bank_occupancy_[itC][itB]) {
                std::cout << clk_<<"\tGetReadyCommandPIM fail(" << ready_cmd << ") - Bank occupancy " << itC << itB << ": " << bank_occupancy_[itC][itB] << '\n';
                return Command();
            }
        }
    }
    return ready_cmd;
}



IdealDRAMSystem::IdealDRAMSystem(Config &config, const std::string &output_dir,
                                 std::function<void(uint64_t)> read_callback,
                                 std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config, output_dir, read_callback, write_callback),
      latency_(config_.ideal_memory_latency) {}

IdealDRAMSystem::~IdealDRAMSystem() {}

bool IdealDRAMSystem::AddTransaction(uint64_t hex_addr, bool is_write) {
    auto trans = Transaction(hex_addr, is_write);
    trans.added_cycle = clk_;
    infinite_buffer_q_.push_back(trans);
    return true;
}

void IdealDRAMSystem::ClockTick() {
    for (auto trans_it = infinite_buffer_q_.begin();
         trans_it != infinite_buffer_q_.end();) {
        if (clk_ - trans_it->added_cycle >= static_cast<uint64_t>(latency_)) {
            if (trans_it->is_write) {
                write_callback_(trans_it->addr);
            } else {
                read_callback_(trans_it->addr);
            }
            trans_it = infinite_buffer_q_.erase(trans_it++);
        }
        if (trans_it != infinite_buffer_q_.end()) {
            ++trans_it;
        }
    }

    clk_++;
    return;
}

}  // namespace dramsim3
