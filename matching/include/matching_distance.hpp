namespace md {

    template<class K, class V>
    void print_map(const std::map<K, V>& dic)
    {
        for(const auto kv : dic) {
            fmt::print("{} -> {}\n", kv.first, kv.second);
        }
    }

    template<class T>
    void DistanceCalculator<T>::check_upper_bound(const CellWithValue& dual_cell) const
    {
        spd::debug("Enter check_get_max_delta_on_cell");
        const int n_samples_lambda = 100;
        const int n_samples_mu = 100;
        DualBox db = dual_cell.dual_box();
        Real min_lambda = db.lambda_min();
        Real max_lambda = db.lambda_max();
        Real min_mu = db.mu_min();
        Real max_mu = db.mu_max();

        Real h_lambda = (max_lambda - min_lambda) / n_samples_lambda;
        Real h_mu = (max_mu - min_mu) / n_samples_mu;
        for(int i = 1; i < n_samples_lambda; ++i) {
            for(int j = 1; j < n_samples_mu; ++j) {
                Real lambda = min_lambda + i * h_lambda;
                Real mu = min_mu + j * h_mu;
                DualPoint l(db.axis_type(), db.angle_type(), lambda, mu);
                Real other_result = distance_on_line_const(l);
                Real diff = fabs(dual_cell.stored_upper_bound() - other_result);
                if (other_result > dual_cell.stored_upper_bound()) {
                    spd::error(
                            "in check_upper_bound, upper_bound = {}, other_result = {}, diff = {}\ndual_cell = {}",
                            dual_cell.stored_upper_bound(), other_result, diff, dual_cell);
                    throw std::runtime_error("Wrong delta estimate");
                }
            }
        }
        spd::debug("Exit check_get_max_delta_on_cell");
    }

    // for all lines l, l' inside dual box,
    // find the upper bound on the difference of weighted  pushes of p
    template<class T>
    Real
    DistanceCalculator<T>::get_max_displacement_single_point(const CellWithValue& dual_cell, ValuePoint vp,
            const Point& p) const
    {
        assert(p.x >= 0 && p.y >= 0);

#ifdef MD_DEBUG
        std::vector<long long int> debug_ids = {3, 13, 54, 218, 350, 382, 484, 795, 2040, 8415, 44076};
        bool debug = false; // std::find(debug_ids.begin(), debug_ids.end(), dual_cell.id) != debug_ids.end();
#endif
        DualPoint line = dual_cell.value_point(vp);
        const Real base_value = line.weighted_push(p);

        spd::debug("Enter get_max_displacement_single_point, p = {},\ndual_cell = {},\nline = {}, base_value = {}\n", p,
                dual_cell, line, base_value);

        Real result = 0.0;
        for(DualPoint dp : dual_cell.dual_box().critical_points(p)) {
            Real dp_value = dp.weighted_push(p);
            spd::debug(
                    "In get_max_displacement_single_point, p = {}, critical dp = {},\ndp_value = {}, diff = {},\ndual_cell = {}\n",
                    p, dp, dp_value, fabs(base_value - dp_value), dual_cell);
            result = std::max(result, fabs(base_value - dp_value));
        }

#ifdef MD_DO_FULL_CHECK
        DualBox db = dual_cell.dual_box();
        std::uniform_real_distribution<Real> dlambda(db.lambda_min(), db.lambda_max());
        std::uniform_real_distribution<Real> dmu(db.mu_min(), db.mu_max());
        std::mt19937 gen(1);
        for(int i = 0; i < 1000; ++i) {
            Real lambda = dlambda(gen);
            Real mu = dmu(gen);
            DualPoint dp_random { db.axis_type(), db.angle_type(), lambda, mu };
            Real dp_value = dp_random.weighted_push(p);
            if (fabs(base_value - dp_value) > result) {
                spd::error("in get_max_displacement_single_point, p = {}, vp = {}\ndb = {}\nresult = {}, base_value = {}, dp_value = {}, dp_random = {}",
                        p, vp, db, result, base_value, dp_value, dp_random);
                throw std::runtime_error("error in get_max_displacement_single_value");
            }
        }
#endif

        return result;
    }

    template<class T>
    typename DistanceCalculator<T>::CellValueVector DistanceCalculator<T>::get_initial_dual_grid(Real& lower_bound)
    {
        CellValueVector result = get_refined_grid(params_.initialization_depth, false, true);

        lower_bound = -1.0;
        for(const auto& dc : result) {
            lower_bound = std::max(lower_bound, dc.max_corner_value());
        }

        assert(lower_bound >= 0);

        for(auto& dual_cell : result) {
            Real good_enough_ub = get_good_enough_upper_bound(lower_bound);
            Real max_value_on_cell = get_upper_bound(dual_cell, good_enough_ub);
            dual_cell.set_max_possible_value(max_value_on_cell);

#ifdef MD_DO_FULL_CHECK
            check_upper_bound(dual_cell);
#endif

            spd::debug("DEBUG INIT: added cell {}", dual_cell);
        }

        return result;
    }

    template<class T>
    typename DistanceCalculator<T>::CellValueVector
    DistanceCalculator<T>::get_refined_grid(int init_depth, bool calculate_on_intermediate, bool calculate_on_last)
    {
        const Real y_max = std::max(module_a_.max_y(), module_b_.max_y());
        const Real x_max = std::max(module_a_.max_x(), module_b_.max_x());

        const Real lambda_min = 0;
        const Real lambda_max = 1;

        const Real mu_min = 0;

        DualBox x_flat(DualPoint(AxisType::x_type, AngleType::flat, lambda_min, mu_min),
                DualPoint(AxisType::x_type, AngleType::flat, lambda_max, x_max));

        DualBox x_steep(DualPoint(AxisType::x_type, AngleType::steep, lambda_min, mu_min),
                DualPoint(AxisType::x_type, AngleType::steep, lambda_max, x_max));

        DualBox y_flat(DualPoint(AxisType::y_type, AngleType::flat, lambda_min, mu_min),
                DualPoint(AxisType::y_type, AngleType::flat, lambda_max, y_max));

        DualBox y_steep(DualPoint(AxisType::y_type, AngleType::steep, lambda_min, mu_min),
                DualPoint(AxisType::y_type, AngleType::steep, lambda_max, y_max));

        CellWithValue x_flat_cell(x_flat, 0);
        CellWithValue x_steep_cell(x_steep, 0);
        CellWithValue y_flat_cell(y_flat, 0);
        CellWithValue y_steep_cell(y_steep, 0);

        if (init_depth == 0) {
            DualPoint diagonal_x_flat(AxisType::x_type, AngleType::flat, 1, 0);

            Real diagonal_value = distance_on_line(diagonal_x_flat);
            n_hera_calls_per_level_[0]++;

            x_flat_cell.set_value_at(ValuePoint::lower_right, diagonal_value);
            y_flat_cell.set_value_at(ValuePoint::lower_right, diagonal_value);
            x_steep_cell.set_value_at(ValuePoint::lower_right, diagonal_value);
            y_steep_cell.set_value_at(ValuePoint::lower_right, diagonal_value);
        }

#ifdef MD_DEBUG
        x_flat_cell.id = 1;
        x_steep_cell.id = 2;
        y_flat_cell.id = 3;
        y_steep_cell.id = 4;
        CellWithValue::max_id = 4;
#endif

        CellValueVector result {x_flat_cell, x_steep_cell, y_flat_cell, y_steep_cell};

        if (init_depth == 0) {
            return result;
        }

        CellValueVector refined_result;

        for(int i = 1; i <= init_depth; ++i) {
            refined_result.clear();
            for(const auto& dual_cell : result) {
                for(auto refined_cell : dual_cell.get_refined_cells()) {
                    // we calculate for init_dept - 1, not init_depth,
                    // because we want the cells to have value at a corner
                    if ((i == init_depth - 1 and calculate_on_last) or calculate_on_intermediate)
                        set_cell_central_value(refined_cell);
                    refined_result.push_back(refined_cell);
                }
            }
            result = std::move(refined_result);
        }
        return result;
    }

    template<class T>
    DistanceCalculator<T>::DistanceCalculator(const T& a,
            const T& b,
            CalculationParams& params)
            :
            module_a_(a),
            module_b_(b),
            params_(params)
    {
        // make all coordinates non-negative
        auto min_coord = std::min(module_a_.minimal_coordinate(),
                module_b_.minimal_coordinate());
        if (min_coord < 0) {
            module_a_.translate(-min_coord);
            module_b_.translate(-min_coord);
        }

        assert(std::min({module_a_.min_x(), module_b_.min_x(), module_a_.min_y(),
                         module_b_.min_y()}) >= 0);

        spd::info("DistanceCalculator constructed, module_a: max_x = {}, max_y = {}, module_b: max_x = {}, max_y = {}",
                module_a_.max_x(), module_a_.max_y(), module_b_.max_x(), module_b_.max_y());
    }

    template<class T>
    Real DistanceCalculator<T>::get_max_x(int module) const
    {
        return (module == 0) ? module_a_.max_x() : module_b_.max_x();
    }

    template<class T>
    Real DistanceCalculator<T>::get_max_y(int module) const
    {
        return (module == 0) ? module_a_.max_y() : module_b_.max_y();
    }

    template<class T>
    Real
    DistanceCalculator<T>::get_local_refined_bound(const md::DualBox& dual_box) const
    {
        return get_local_refined_bound(0, dual_box) + get_local_refined_bound(1, dual_box);
    }

    template<class T>
    Real
    DistanceCalculator<T>::get_local_refined_bound(int module, const md::DualBox& dual_box) const
    {
        spd::debug("Enter get_local_refined_bound, dual_box = {}", dual_box);
        Real d_lambda = dual_box.lambda_max() - dual_box.lambda_min();
        Real d_mu = dual_box.mu_max() - dual_box.mu_min();
        Real result;
        if (dual_box.axis_type() == AxisType::x_type) {
            if (dual_box.is_flat()) {
                result = dual_box.lambda_max() * d_mu + (get_max_x(module) - dual_box.mu_min()) * d_lambda;
            } else {
                result = d_mu + get_max_y(module) * d_lambda;
            }
        } else {
            // y-type
            if (dual_box.is_flat()) {
                result = d_mu + get_max_x(module) * d_lambda;
            } else {
                // steep
                result = dual_box.lambda_max() * d_mu + (get_max_y(module) - dual_box.mu_min()) * d_lambda;
            }
        }
        return result;
    }

    template<class T>
    Real DistanceCalculator<T>::get_local_dual_bound(int module, const md::DualBox& dual_box) const
    {
        Real dlambda = dual_box.lambda_max() - dual_box.lambda_min();
        Real dmu = dual_box.mu_max() - dual_box.mu_min();

        if (dual_box.is_flat()) {
            return get_max_x(module) * dlambda + dmu;
        } else {
            return get_max_y(module) * dlambda + dmu;
        }
    }

    template<class T>
    Real DistanceCalculator<T>::get_local_dual_bound(const md::DualBox& dual_box) const
    {
        return get_local_dual_bound(0, dual_box) + get_local_dual_bound(1, dual_box);
    }

    template<class T>
    Real DistanceCalculator<T>::get_upper_bound(const CellWithValue& dual_cell, Real good_enough_ub) const
    {
        assert(good_enough_ub >= 0);

        switch(params_.bound_strategy) {
            case BoundStrategy::bruteforce:
                return std::numeric_limits<Real>::max();

            case BoundStrategy::local_dual_bound:
                return dual_cell.min_value() + get_local_dual_bound(dual_cell.dual_box());

            case BoundStrategy::local_dual_bound_refined:
                return dual_cell.min_value() + get_local_refined_bound(dual_cell.dual_box());

            case BoundStrategy::local_combined: {
                Real cheap_upper_bound = dual_cell.min_value() + get_local_refined_bound(dual_cell.dual_box());
                if (cheap_upper_bound < good_enough_ub) {
                    return cheap_upper_bound;
                } else {
                    [[fallthrough]];
                }
            }

            case BoundStrategy::local_dual_bound_for_each_point: {
                Real result = std::numeric_limits<Real>::max();
                for(ValuePoint vp : k_corner_vps) {
                    if (not dual_cell.has_value_at(vp)) {
                        continue;
                    }

                    Real base_value = dual_cell.value_at(vp);
                    Real bound_dgm_a = get_single_dgm_bound(dual_cell, vp, 0, good_enough_ub);

                    if (params_.stop_asap and bound_dgm_a + base_value >= good_enough_ub) {
                        // we want to return a valid upper bound, not just something that will prevent discarding the cell
                        // and we don't want to compute pushes for points in second bifiltration.
                        // so just return a constant time bound
                        return dual_cell.min_value() + get_local_refined_bound(dual_cell.dual_box());
                    }

                    Real bound_dgm_b = get_single_dgm_bound(dual_cell, vp, 1,
                            std::max(Real(0), good_enough_ub - bound_dgm_a));

                    result = std::min(result, base_value + bound_dgm_a + bound_dgm_b);

#ifdef MD_DEBUG
                    spd::debug("In get_upper_bound, cell = {}", dual_cell);
                    spd::debug("In get_upper_bound, vp = {}, base_value = {}, bound_dgm_a = {}, bound_dgm_b = {}, result = {}", vp, base_value, bound_dgm_a, bound_dgm_b, result);
#endif

                    if (params_.stop_asap and result < good_enough_ub) {
                        break;
                    }
                }
                return result;
            }
        }
        // to suppress compiler warning
        return std::numeric_limits<Real>::max();
    }

    // find maximal displacement of weighted points of m for all lines in dual_box
    template<class T>
    Real
    DistanceCalculator<T>::get_single_dgm_bound(const CellWithValue& dual_cell,
            ValuePoint vp,
            int module,
            [[maybe_unused]] Real good_enough_value) const
    {
        Real result = 0;
        Point max_point;

        spd::debug(
                "Enter get_single_dgm_bound, module = {}, dual_cell = {}, vp = {}, good_enough_value = {}, stop_asap = {}\n",
                module, dual_cell, vp, good_enough_value, params_.stop_asap);

        const T& m = (module == 0) ? module_a_ : module_b_;
        for(const auto& position : m.positions()) {
            spd::debug("in get_single_dgm_bound, simplex = {}\n", position);

            Real x = get_max_displacement_single_point(dual_cell, vp, position);

            spd::debug("In get_single_dgm_bound, point = {}, displacement = {}", position, x);

            if (x > result) {
                result = x;
                max_point = position;
                spd::debug("In get_single_dgm_bound, point = {}, result now = displacement = {}", position, x);
            }

            if (params_.stop_asap and result > good_enough_value) {
                // we want to return a valid upper bound,
                // now we just see it is worse than we need, but it may be even more
                // just return a valid upper bound
                spd::debug("result {} > good_enough_value {}, exit and return refined bound {}", result,
                        good_enough_value, get_local_refined_bound(dual_cell.dual_box()));
                result = get_local_refined_bound(dual_cell.dual_box());
                break;
            }
        }

        spd::debug("Exit get_single_dgm_bound,\ndual_cell = {}\nmodule = {}, result = {}, max_point = {}", dual_cell,
                module, result, max_point);

        return result;
    }

    template<class T>
    Real DistanceCalculator<T>::distance()
    {
        return get_distance_pq();
    }

    // calculate weighted bottleneneck distance between slices on line
    // increments hera calls counter
    template<class T>
    Real DistanceCalculator<T>::distance_on_line(DualPoint line)
    {
        ++n_hera_calls_;
        Real result = distance_on_line_const(line);
        return result;
    }

    template<class T>
    Real DistanceCalculator<T>::distance_on_line_const(DualPoint line) const
    {
        // TODO: think about this - how to call Hera
        auto dgm_a = module_a_.weighted_slice_diagram(line);
        auto dgm_b = module_b_.weighted_slice_diagram(line);
        Real result;
        if (params_.hera_epsilon > static_cast<Real>(0)) {
            result = hera::bottleneckDistApprox(dgm_a, dgm_b, params_.hera_epsilon) / ( params_.hera_epsilon + 1);
        } else {
            result = hera::bottleneckDistExact(dgm_a, dgm_b);
        }
        if (n_hera_calls_ % 100 == 1) {
            spd::debug("Calling Hera, dgm_a.size = {}, dgm_b.size = {}, line = {}, result = {}", dgm_a.size(),
                    dgm_b.size(), line, result);
        } else {
            spd::debug("Calling Hera, dgm_a.size = {}, dgm_b.size = {}, line = {}, result = {}", dgm_a.size(),
                    dgm_b.size(), line, result);
        }
        return result;
    }

    template<class T>
    Real DistanceCalculator<T>::get_good_enough_upper_bound(Real lower_bound) const
    {
        Real result;
        // in upper_bound strategy we only prune cells if they cannot improve the lower bound,
        // otherwise the experiment is supposed to run indefinitely
        if (params_.traverse_strategy == TraverseStrategy::upper_bound) {
            result = lower_bound;
        } else {
            result = (1.0 + params_.delta) * lower_bound;
        }
        return result;
    }

    // helper function
    // calculate weighted bt distance on cell center,
    // assign distance value to cell, keep it in heat_map, and return
    template<class T>
    void DistanceCalculator<T>::set_cell_central_value(CellWithValue& dual_cell)
    {
        DualPoint central_line {dual_cell.center()};

        spd::debug("In set_cell_central_value, processing dual cell = {}, line = {}", dual_cell.dual_box(),
                central_line);
        Real new_value = distance_on_line(central_line);
        n_hera_calls_per_level_[dual_cell.level() + 1]++;
        dual_cell.set_value_at(ValuePoint::center, new_value);
        params_.actual_max_depth = std::max(params_.actual_max_depth, dual_cell.level() + 1);

#ifdef PRINT_HEAT_MAP
        if (params_.bound_strategy == BoundStrategy::bruteforce) {
            spd::debug("In set_cell_central_value, adding to heat_map pair {} - {}", dual_cell.center(), new_value);
            if (dual_cell.level() > params_.initialization_depth + 1
                    and params_.heat_maps[dual_cell.level()].count(dual_cell.center()) > 0) {
                auto existing = params_.heat_maps[dual_cell.level()].find(dual_cell.center());
                spd::debug("EXISTING: {} -> {}", existing->first, existing->second);
            }
            assert(dual_cell.level() <= params_.initialization_depth + 1
                    or params_.heat_maps[dual_cell.level()].count(dual_cell.center()) == 0);
            params_.heat_maps[dual_cell.level()][dual_cell.center()] = new_value;
        }
#endif
    }

    // quick-and-dirty hack to efficiently traverse priority queue with dual cells
    // returns maximal possible value on all cells in queue
    // assumes that the underlying container is vector!
    // cell_ptr: pointer to the first element in queue
    // n_cells: queue size
    template<class T>
    Real DistanceCalculator<T>::get_max_possible_value(const CellWithValue* cell_ptr, int n_cells)
    {
        Real result = (n_cells > 0) ? cell_ptr->stored_upper_bound() : 0;
        for(int i = 0; i < n_cells; ++i, ++cell_ptr) {
            result = std::max(result, cell_ptr->stored_upper_bound());
        }
        return result;
    }

    // helper function:
    // return current error from lower and upper bounds
    // and save it in params_ (hence not const)
    template<class T>
    Real DistanceCalculator<T>::current_error(Real lower_bound, Real upper_bound)
    {
        Real current_error = (lower_bound > 0.0) ? (upper_bound - lower_bound) / lower_bound
                                                 : std::numeric_limits<Real>::max();

        params_.actual_error = current_error;

        if (current_error < params_.delta) {
            spd::debug(
                    "Threshold achieved! bound_strategy = {}, traverse_strategy = {}, upper_bound = {}, current_error = {}",
                    params_.bound_strategy, params_.traverse_strategy, upper_bound, current_error);
        }
        return current_error;
    }

    // return matching distance
    // use priority queue to store dual cells
    // comparison function depends on the strategies in params_
    // ressets hera calls counter
    template<class T>
    Real DistanceCalculator<T>::get_distance_pq()
    {
        std::map<int, long> n_cells_considered;
        std::map<int, long> n_cells_pushed_into_queue;
        long int n_too_deep_cells = 0;
        std::map<int, long> n_cells_discarded;
        std::map<int, long> n_cells_pruned;

        spd::info("Enter get_distance_pq, bound strategy = {}, traverse strategy = {}, stop_asap = {} ",
                params_.bound_strategy, params_.traverse_strategy, params_.stop_asap);

        std::chrono::high_resolution_clock timer;
        auto start_time = timer.now();

        n_hera_calls_ = 0;
        n_hera_calls_per_level_.clear();


        // if cell is too deep and is not pushed into queue,
        // we still need to take its max value into account;
        // the max over such cells is stored in max_result_on_too_fine_cells
        Real upper_bound_on_deep_cells = -1;

        spd::debug("Started iterations in dual space, delta = {}, bound_strategy = {}", params_.delta,
                params_.bound_strategy);
        // user-defined less lambda function
        // to regulate priority queue depending on strategy
        auto dual_cell_less = [this](const CellWithValue& a, const CellWithValue& b) {

            int a_level = a.level();
            int b_level = b.level();
            Real a_value = a.max_corner_value();
            Real b_value = b.max_corner_value();
            Real a_ub = a.stored_upper_bound();
            Real b_ub = b.stored_upper_bound();
            if (this->params_.traverse_strategy == TraverseStrategy::upper_bound and
                    (not a.has_max_possible_value() or not b.has_max_possible_value())) {
                throw std::runtime_error("no upper bound on cell");
            }
            DualPoint a_lower_left = a.dual_box().lower_left();
            DualPoint b_lower_left = b.dual_box().lower_left();

            switch(this->params_.traverse_strategy) {
                // in both breadth_first searches we want coarser cells
                // to be processed first. Cells with smaller level must be larger,
                // hence the minus in front of level
                case TraverseStrategy::breadth_first:
                    return std::make_tuple(-a_level, a_lower_left)
                            < std::make_tuple(-b_level, b_lower_left);
                case TraverseStrategy::breadth_first_value:
                    return std::make_tuple(-a_level, a_value, a_lower_left)
                            < std::make_tuple(-b_level, b_value, b_lower_left);
                case TraverseStrategy::depth_first:
                    return std::make_tuple(a_value, a_level, a_lower_left)
                            < std::make_tuple(b_value, b_level, b_lower_left);
                case TraverseStrategy::upper_bound:
                    return std::make_tuple(a_ub, a_level, a_lower_left)
                            < std::make_tuple(b_ub, b_level, b_lower_left);
                default:
                    throw std::runtime_error("Forgotten case");
            }
        };

        std::priority_queue<CellWithValue, CellValueVector, decltype(dual_cell_less)> dual_cells_queue(
                dual_cell_less);

        // weighted bt distance on the center of current cell
        Real lower_bound = std::numeric_limits<Real>::min();

        // init pq and lower bound
        for(auto& init_cell : get_initial_dual_grid(lower_bound)) {
            dual_cells_queue.push(init_cell);
        }

        Real upper_bound = get_max_possible_value(&dual_cells_queue.top(), dual_cells_queue.size());

        std::vector<UbExperimentRecord> ub_experiment_results;

        while(not dual_cells_queue.empty()) {

            CellWithValue dual_cell = dual_cells_queue.top();
            dual_cells_queue.pop();
            assert(dual_cell.has_corner_value()
                    and dual_cell.has_max_possible_value()
                    and dual_cell.max_corner_value() <= upper_bound);

            n_cells_considered[dual_cell.level()]++;

            bool discard_cell = false;

            if (not params_.stop_asap) {
                // if stop_asap is on, it is safer to never discard a cell
                if (params_.bound_strategy == BoundStrategy::bruteforce) {
                    discard_cell = false;
                } else if (params_.traverse_strategy == TraverseStrategy::upper_bound) {
                    discard_cell = (dual_cell.stored_upper_bound() <= lower_bound);
                } else {
                    discard_cell = (dual_cell.stored_upper_bound() <= (1.0 + params_.delta) * lower_bound);
                }
            }

            spd::debug(
                    "CURRENT CELL bound_strategy = {}, traverse_strategy = {}, dual cell: {}, upper_bound = {}, lower_bound = {}, current_error = {}, discard_cell = {}",
                    params_.bound_strategy, params_.traverse_strategy, dual_cell, upper_bound, lower_bound,
                    current_error(lower_bound, upper_bound), discard_cell);

            if (discard_cell) {
                n_cells_discarded[dual_cell.level()]++;
                continue;
            }

            // until now, dual_cell knows its value in one of its corners
            // new_value will be the weighted distance at its center
            set_cell_central_value(dual_cell);
            Real new_value = dual_cell.value_at(ValuePoint::center);
            lower_bound = std::max(new_value, lower_bound);

            spd::debug("Processed cell = {}, weighted value = {}, lower_bound = {}", dual_cell, new_value, lower_bound);

            assert(upper_bound >= lower_bound);

            if (current_error(lower_bound, upper_bound) < params_.delta) {
                break;
            }

            // refine cell and push 4 smaller cells into queue
            for(auto refined_cell : dual_cell.get_refined_cells()) {

                if (refined_cell.num_values() == 0)
                    throw std::runtime_error("no value on cell");

                // if delta is smaller than good_enough_value, it allows to prune cell
                Real good_enough_ub = get_good_enough_upper_bound(lower_bound);

                // upper bound of the parent holds for refined_cell
                // and can sometimes be smaller!
                Real upper_bound_on_refined_cell = std::min(dual_cell.stored_upper_bound(),
                        get_upper_bound(refined_cell, good_enough_ub));

                spd::debug("upper_bound_on_refined_cell = {},  dual_cell.stored_upper_bound = {}, get_upper_bound = {}",
                        upper_bound_on_refined_cell, dual_cell.stored_upper_bound(),
                        get_upper_bound(refined_cell, good_enough_ub));

                refined_cell.set_max_possible_value(upper_bound_on_refined_cell);

#ifdef MD_DO_FULL_CHECK
                check_upper_bound(refined_cell);
#endif

                bool prune_cell = false;

                if (refined_cell.level() <= params_.max_depth) {
                    // cell might be added to queue; if it is not added, its maximal value can be safely ignored
                    if (params_.traverse_strategy == TraverseStrategy::upper_bound) {
                        prune_cell = (refined_cell.stored_upper_bound() <= lower_bound);
                    } else if (params_.bound_strategy != BoundStrategy::bruteforce) {
                        prune_cell = (refined_cell.stored_upper_bound() <= (1.0 + params_.delta) * lower_bound);
                    }
                    if (prune_cell)
                        n_cells_pruned[refined_cell.level()]++;
//                        prune_cell = (max_result_on_refined_cell <= lower_bound);
                } else {
                    // cell is too deep, it won't be added to queue
                    // we must memorize maximal value on this cell, because we won't see it anymore
                    prune_cell = true;
                    if (refined_cell.stored_upper_bound() > (1 + params_.delta) * lower_bound) {
                        n_too_deep_cells++;
                    }
                    upper_bound_on_deep_cells = std::max(upper_bound_on_deep_cells, refined_cell.stored_upper_bound());
                }

                spd::debug(
                        "In get_distance_pq, loop over refined cells, bound_strategy = {}, traverse_strategy = {}, refined cell: {}, max_value_on_cell = {}, upper_bound = {}, current_error = {}, prune_cell = {}",
                        params_.bound_strategy, params_.traverse_strategy, refined_cell,
                        refined_cell.stored_upper_bound(), upper_bound, current_error(lower_bound, upper_bound),
                        prune_cell);

                if (not prune_cell) {
                    n_cells_pushed_into_queue[refined_cell.level()]++;
                    dual_cells_queue.push(refined_cell);
                }
            } // end loop over refined cells

            if (dual_cells_queue.empty())
                upper_bound = std::max(upper_bound, upper_bound_on_deep_cells);
            else
                upper_bound = std::max(upper_bound_on_deep_cells,
                        get_max_possible_value(&dual_cells_queue.top(), dual_cells_queue.size()));

            if (params_.traverse_strategy == TraverseStrategy::upper_bound) {
                upper_bound = dual_cells_queue.top().stored_upper_bound();

                if (get_hera_calls_number() < 20 || get_hera_calls_number() % 20 == 0) {
                    auto elapsed = timer.now() - start_time;
                    UbExperimentRecord ub_exp_record;

                    ub_exp_record.error = current_error(lower_bound, upper_bound);
                    ub_exp_record.lower_bound = lower_bound;
                    ub_exp_record.upper_bound = upper_bound;
                    ub_exp_record.cell = dual_cells_queue.top();
                    ub_exp_record.n_hera_calls = n_hera_calls_;
                    ub_exp_record.time = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

#ifdef MD_DO_CHECKS
                    if (ub_experiment_results.size() > 0) {
                        auto prev = ub_experiment_results.back();
                        if (upper_bound > prev.upper_bound) {
                            spd::error("ALARM 1, upper_bound = {}, top = {}, prev.ub = {}, prev cell = {}, lower_bound = {}, prev.lower_bound = {}",
                                    upper_bound, ub_exp_record.cell, prev.upper_bound, prev.cell, lower_bound, prev.lower_bound);
                            throw std::runtime_error("die");
                        }

                        if (lower_bound < prev.lower_bound) {
                            spd::error("ALARM 2, lower_bound = {}, prev.lower_bound = {}, top = {}, prev.ub = {}, prev cell = {}", lower_bound, prev.lower_bound, ub_exp_record.cell, prev.upper_bound, prev.cell);
                            throw std::runtime_error("die");
                        }
                    }
#endif

                    ub_experiment_results.emplace_back(ub_exp_record);

                    fmt::print(std::cerr, "[UB_EXPERIMENT]\t{}\n", ub_exp_record);
                }
            }

            assert(upper_bound >= lower_bound);

            if (current_error(lower_bound, upper_bound) < params_.delta) {
                break;
            }
        }

        params_.actual_error = current_error(lower_bound, upper_bound);

        if (n_too_deep_cells > 0) {
            spd::warn(
                    "Error not guaranteed, there were {} too deep cells. Actual error = {}. Increase max_depth or delta",
                    n_too_deep_cells, params_.actual_error);
        }
        // otherwise actual_error in params can be larger than delta,
        // but this is OK

        spd::info("#############################################################");
        spd::info(
                "Exiting get_distance_pq, bound_strategy = {}, traverse_strategy = {}, lower_bound = {}, upper_bound = {}, current_error = {}, actual_max_level = {}",
                params_.bound_strategy, params_.traverse_strategy, lower_bound,
                upper_bound, params_.actual_error, params_.actual_max_depth);

        spd::info("#############################################################");

        bool print_stats = true;
        if (print_stats) {
            fmt::print("EXIT STATS, cells considered:\n");
            print_map(n_cells_considered);
            fmt::print("EXIT STATS, cells discarded:\n");
            print_map(n_cells_discarded);
            fmt::print("EXIT STATS, cells pruned:\n");
            print_map(n_cells_pruned);
            fmt::print("EXIT STATS, cells pushed:\n");
            print_map(n_cells_pushed_into_queue);
            fmt::print("EXIT STATS, hera calls:\n");
            print_map(n_hera_calls_per_level_);

            fmt::print("EXIT STATS, too deep cells with high value: {}\n", n_too_deep_cells);
        }

        return lower_bound;
    }

    template<class T>
    int DistanceCalculator<T>::get_hera_calls_number() const
    {
        return n_hera_calls_;
    }

}