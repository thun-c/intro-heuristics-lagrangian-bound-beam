#pragma once
#pragma GCC optimize("Ofast")
#include <bits/stdc++.h>
using namespace std;

/*************************************************************************
 * 価格安定化ラグランジュ緩和 + 複数境界ビームサーチライブラリ
 *
 * Problem は以下のインターフェースを実装する。
 * 仮想関数は使わず、LagBeamProblem conceptでコンパイル時に検査する。
 *
 * using Answer
 * using DualActivity; static constexpr bool MAXIMIZE
 * int dualDimension() const; vector<double> initialDualPrices() const
 * DualActivity makeDualActivity() const
 * int relaxedBlockCount() const
 * ScoredAnswer<Answer> initialIncumbent() const
 * double dualConstant(const vector<double>&) const
 * double relaxedBlockValue(int, const vector<double>&, DualActivity*) const
 * void computeDualImprovementDirection(const DualActivity&,
 *                                      vector<double>&) const
 * void projectDualPrices(vector<double>&) const
 * SearchModel prepareSearch(const vector<vector<double>>&) const
 *
 * SearchModel は以下のインターフェースを実装する。
 * LagBeamSearchModel conceptでコンパイル時に検査する。
 *
 * using State, Action, Answer; static constexpr bool MAXIMIZE
 * int stageCount/cutCount/maxActions() const
 * State initialState(); uint64_t initialHash()
 * const vector<Action>& actions(int stage)
 * bool isFeasible(stage, state, action)
 * int64_t childAcc(stage, state, acc, action)
 * uint64_t childHash(stage, state, hash, action)
 * void apply(State&, stage, action)
 * double parentFuture(cut, next_stage, state)
 * double childFuture(cut, next_stage, state, action, parent_future)
 * double suffixPrice(cut, next_stage)
 * int64_t finalScore(state, acc)
 * ScoredAnswer<Answer> roundedAnswer()
 * Answer makeAnswer(const vector<Action>&); int64_t scoreAnswer(const Answer&)
 *************************************************************************/
namespace lagbeam
{

// 双対価格更新の設定。
struct DualConfig
{
    int iterations_ = 60;
    double initial_step_scale_ = 1.15;
    double direction_memory_ = 0.35;
    double recenter_weight_ = 0.65;
    int stall_iterations_ = 3;
    double min_step_scale_ = 0.45;
    double max_step_scale_ = 1.35;
    double step_growth_ = 1.04;
    double stalled_step_decay_ = 0.72;
    double min_step_ = 5.0;
    double initial_step_cap_ = 1500.0;
    double final_step_cap_ = 300.0;
};

// 保存した価格ベクトルから上界を選ぶ設定。
struct CutConfig
{
    int count_ = 3;
    int candidate_pool_ = 14;
    double diversity_loss_scale_ = 5000.0;
};

// 複数上界ビームサーチの設定。
struct BeamConfig
{
    int width_ = 14500;
    int preselect_factor_ = 2;
    int hash_table_size_ = 1 << 15;
    double incumbent_epsilon_ = 1e-7;
};

struct Config
{
    DualConfig dual_;
    CutConfig cuts_;
    BeamConfig beam_;
};

template <class Answer> struct ScoredAnswer
{
    Answer answer_;
    int64_t score_ = numeric_limits<int64_t>::lowest();
};

// 双対価格更新とSearchModel生成に必要な、問題側アダプタの契約。
template <class Problem>
concept LagBeamProblem =
    requires(const Problem& problem, const vector<double>& prices, vector<double>& direction,
             const vector<vector<double>>& selected_prices,
             typename Problem::DualActivity& activity, int block) {
        typename Problem::Answer;
        typename Problem::DualActivity;
        typename bool_constant<Problem::MAXIMIZE>;
        { Problem::MAXIMIZE } -> convertible_to<bool>;
        requires same_as<remove_cv_t<decltype(Problem::MAXIMIZE)>, bool>;
        { problem.dualDimension() } -> convertible_to<int>;
        { problem.initialDualPrices() } -> same_as<vector<double>>;
        { problem.makeDualActivity() } -> same_as<typename Problem::DualActivity>;
        { problem.relaxedBlockCount() } -> convertible_to<int>;
        { problem.initialIncumbent() } -> same_as<ScoredAnswer<typename Problem::Answer>>;
        { problem.dualConstant(prices) } -> convertible_to<double>;
        { problem.relaxedBlockValue(block, prices, &activity) } -> convertible_to<double>;
        { problem.computeDualImprovementDirection(activity, direction) } -> same_as<void>;
        { problem.projectDualPrices(direction) } -> same_as<void>;
        problem.prepareSearch(selected_prices);
    };

// 複数境界ビームサーチに必要な、探索側アダプタの契約。
template <class Model>
concept LagBeamSearchModel =
    requires(const Model& model, typename Model::State& state,
             const typename Model::State& const_state, typename Model::Action action,
             const vector<typename Model::Action>& action_history, int stage, int cut, int64_t acc,
             uint64_t hash, double parent_future) {
        typename Model::Answer;
        typename Model::State;
        typename Model::Action;
        typename bool_constant<Model::MAXIMIZE>;
        { Model::MAXIMIZE } -> convertible_to<bool>;
        requires same_as<remove_cv_t<decltype(Model::MAXIMIZE)>, bool>;
        { model.stageCount() } -> convertible_to<int>;
        { model.cutCount() } -> convertible_to<int>;
        { model.maxActions() } -> convertible_to<int>;
        { model.initialState() } -> same_as<typename Model::State>;
        { model.initialHash() } -> convertible_to<uint64_t>;
        { model.actions(stage) } -> ranges::input_range;
        requires convertible_to<ranges::range_value_t<decltype(model.actions(stage))>,
                                typename Model::Action>;
        { model.isFeasible(stage, const_state, action) } -> convertible_to<bool>;
        { model.childAcc(stage, const_state, acc, action) } -> convertible_to<int64_t>;
        { model.childHash(stage, const_state, hash, action) } -> convertible_to<uint64_t>;
        { model.apply(state, stage, action) } -> same_as<void>;
        { model.parentFuture(cut, stage, const_state) } -> convertible_to<double>;
        { model.childFuture(cut, stage, const_state, action, parent_future) } ->
            convertible_to<double>;
        { model.suffixPrice(cut, stage) } -> convertible_to<double>;
        { model.finalScore(const_state, acc) } -> convertible_to<int64_t>;
        { model.roundedAnswer() } -> same_as<ScoredAnswer<typename Model::Answer>>;
        { model.makeAnswer(action_history) } -> same_as<typename Model::Answer>;
        { model.scoreAnswer(model.makeAnswer(action_history)) } -> convertible_to<int64_t>;
    };

// 1回の双対反復で得た境界値と価格ベクトル。
struct DualSnapshot
{
    double bound_;
    vector<double> prices_;
};

// 双対価格更新の結果。
struct DualResult
{
    vector<double> best_prices_;
    double best_bound_ = 0.0;
    vector<DualSnapshot> snapshots_;
};

// 外部へ返す解と、各構築方法の比較用スコア。
template <class Answer> struct Result
{
    ScoredAnswer<Answer> best_;
    ScoredAnswer<Answer> initial_;
    ScoredAnswer<Answer> rounded_;
    ScoredAnswer<Answer> beam_;
    double dual_bound_ = 0.0;
};

// 目的関数の向きに応じて、lhsがrhsよりよいかを判定する。
template <class Model, class Number> inline bool betterObjective(Number lhs, Number rhs)
{
    if constexpr (Model::MAXIMIZE)
        return lhs > rhs;
    else
        return lhs < rhs;
}

// 最大化なら小さい上界、最小化なら大きい下界をよい双対境界とする。
template <class Model> inline bool betterDualBound(double lhs, double rhs)
{
    if constexpr (Model::MAXIMIZE)
        return lhs < rhs;
    else
        return lhs > rhs;
}

// 複数の双対境界のうち、より厳しいものを採用する。
template <class Model> inline double tightenBound(double current, double candidate)
{
    if constexpr (Model::MAXIMIZE)
        return min(current, candidate);
    else
        return max(current, candidate);
}

// 価格の振動を抑えながら、固定回数だけ双対価格を更新する。
template <LagBeamProblem Problem>
DualResult optimizeDual(const Problem& problem, int64_t incumbent, const DualConfig& cfg)
{
    const int n = problem.dualDimension();
    vector<double> price = problem.initialDualPrices();
    if ((int)price.size() != n)
        throw invalid_argument("lagbeam initial dual price size mismatch");
    problem.projectDualPrices(price);
    vector<double> best_price = price, direction(n, 0.0), raw_direction(n);
    vector<DualSnapshot> snapshots;
    double best_dual = Problem::MAXIMIZE ? 1e100 : -1e100;
    int last_improve = 0;
    double step_scale = cfg.initial_step_scale_;
    int iteration = 0;

    while (iteration < cfg.iterations_)
    {
        // 各部分問題を独立に解き、双対境界と制約の活動量を集計する。
        auto activity = problem.makeDualActivity();
        double dual = problem.dualConstant(price);
        for (int block = 0; block < problem.relaxedBlockCount(); block++)
            dual += problem.relaxedBlockValue(block, price, &activity);

        snapshots.push_back({dual, price});
        if (betterDualBound<Problem>(dual, best_dual))
        {
            best_dual = dual;
            best_price = price;
            last_improve = iteration;
            step_scale = min(cfg.max_step_scale_, step_scale * cfg.step_growth_);
        }

        // 問題側が指定した方向に、過去の方向を混ぜて振動を抑える。
        problem.computeDualImprovementDirection(activity, raw_direction);
        if ((int)raw_direction.size() != n)
            throw invalid_argument("lagbeam dual direction size mismatch");
        double norm = 0.0;
        for (int i = 0; i < n; i++)
            norm += raw_direction[i] * raw_direction[i];
        if (norm < 1e-9)
            break;

        double dot = 0.0;
        for (int i = 0; i < n; i++)
        {
            double g = raw_direction[i];
            direction[i] = cfg.direction_memory_ * direction[i] + (1.0 - cfg.direction_memory_) * g;
            dot += g * direction[i];
        }
        if (dot <= 0.10 * norm)
        {
            dot = norm;
            for (int i = 0; i < n; i++)
                direction[i] = raw_direction[i];
        }

        // 暫定解との差からPolyak型のステップ幅を決める。
        double gap;
        if constexpr (Problem::MAXIMIZE)
            gap = max(0.0, dual - static_cast<double>(incumbent));
        else
            gap = max(0.0, static_cast<double>(incumbent) - dual);
        double step = step_scale * gap / dot;
        double cap = cfg.initial_step_cap_;
        if (cfg.iterations_ > 1)
        {
            cap -=
                (cfg.initial_step_cap_ - cfg.final_step_cap_) * iteration / (cfg.iterations_ - 1.0);
        }
        step = clamp(step, cfg.min_step_, cap);
        for (int i = 0; i < n; i++)
            price[i] += step * direction[i];

        // 改善が止まったら、最良価格の近くへ戻す。
        if (iteration - last_improve >= cfg.stall_iterations_)
        {
            for (int i = 0; i < n; i++)
            {
                price[i] =
                    cfg.recenter_weight_ * best_price[i] + (1.0 - cfg.recenter_weight_) * price[i];
                direction[i] *= 1.0 - cfg.recenter_weight_;
            }
            step_scale = max(cfg.min_step_scale_, step_scale * cfg.stalled_step_decay_);
            last_improve = iteration;
        }
        problem.projectDualPrices(price);
        iteration++;
    }
    return {move(best_price), best_dual, move(snapshots)};
}

// 境界がよく、既に選んだ価格から離れている価格ベクトルを選ぶ。
template <LagBeamProblem Problem>
inline vector<vector<double>> selectPriceCuts(const DualResult& dual, const CutConfig& cfg)
{
    vector<vector<double>> selected{dual.best_prices_};
    if (cfg.count_ <= 1 || dual.snapshots_.size() <= 1)
        return selected;

    vector<int> ids(dual.snapshots_.size());
    iota(ids.begin(), ids.end(), 0);
    sort(ids.begin(), ids.end(),
         [&](int a, int b) {
             return betterDualBound<Problem>(dual.snapshots_[a].bound_, dual.snapshots_[b].bound_);
         });
    const int limit = min<int>(cfg.candidate_pool_, ids.size());
    while ((int)selected.size() < cfg.count_)
    {
        long double best_key = -1.0L;
        vector<double> pick = dual.best_prices_;
        for (int q = 1; q < limit; q++)
        {
            const auto& candidate = dual.snapshots_[ids[q]];
            long double min_distance = 1e100L;
            for (const auto& chosen : selected)
            {
                long double distance = 0.0L;
                for (int i = 0; i < (int)chosen.size(); i++)
                {
                    long double delta = candidate.prices_[i] - chosen[i];
                    distance += delta * delta;
                }
                min_distance = min(min_distance, distance);
            }
            long double loss;
            if constexpr (Problem::MAXIMIZE)
                loss = max(0.0, candidate.bound_ - dual.best_bound_);
            else
                loss = max(0.0, dual.best_bound_ - candidate.bound_);
            long double key = min_distance / (1.0L + loss / cfg.diversity_loss_scale_);
            if (key > best_key)
            {
                best_key = key;
                pick = candidate.prices_;
            }
        }
        selected.push_back(move(pick));
    }
    return selected;
}

// 残りスコアの双対境界を評価値に使い、固定幅で解を構築する。
template <LagBeamSearchModel SearchModel>
ScoredAnswer<typename SearchModel::Answer>
boundedBeamSearch(const SearchModel& model, int64_t incumbent, const BeamConfig& cfg)
{
    using State = typename SearchModel::State;
    using Action = typename SearchModel::Action;
    using Answer = typename SearchModel::Answer;

    // 現在のビームに残す状態。
    struct Node
    {
        int64_t acc_;
        double eval_;
        State state_;
        uint64_t hash_;
    };

    // 次のビームへ追加する候補。状態本体は選抜後にだけコピーする。
    struct Candidate
    {
        int64_t acc_;
        double eval_;
        uint64_t hash_;
        uint16_t parent_;
        Action action_;
    };

    // 最終解を後ろから復元するための情報。
    struct Trace
    {
        uint16_t parent_;
        Action action_;
    };

    const int stages = model.stageCount();
    const int cuts = model.cutCount();
    if (cfg.width_ <= 0 || cfg.preselect_factor_ <= 0)
        throw invalid_argument("lagbeam beam sizes must be positive");
    if (cfg.width_ > numeric_limits<uint16_t>::max())
        throw invalid_argument("lagbeam beam width must be at most 65535");
    if (cfg.hash_table_size_ <= 0 || (cfg.hash_table_size_ & (cfg.hash_table_size_ - 1)) != 0)
        throw invalid_argument("lagbeam hash table size must be a power of two");
    if (cuts <= 0)
        throw invalid_argument("lagbeam needs at least one valid upper bound");
    vector<vector<Trace>> layers;
    layers.reserve(stages);
    vector<Node> beam(1), next_beam;
    beam[0].acc_ = 0;
    beam[0].state_ = model.initialState();
    beam[0].hash_ = model.initialHash();
    beam[0].eval_ = SearchModel::MAXIMIZE ? 1e100 : -1e100;

    // 初期状態では、全ての価格に対する境界を計算する。
    for (int cut = 0; cut < cuts; cut++)
    {
        double bound = model.parentFuture(cut, 0, beam[0].state_) + model.suffixPrice(cut, 0);
        beam[0].eval_ = tightenBound<SearchModel>(beam[0].eval_, bound);
    }

    vector<Candidate> candidates;
    candidates.reserve(cfg.width_ * model.maxActions() + model.maxActions());
    next_beam.reserve(cfg.width_);
    vector<uint64_t> hash_keys(cfg.hash_table_size_);
    vector<int> hash_seen(cfg.hash_table_size_);
    int stamp = 0;

    for (int stage = 0; stage < stages; stage++)
    {
        candidates.clear();
        for (int parent = 0; parent < (int)beam.size(); parent++)
        {
            const auto& node = beam[parent];
            double base = model.parentFuture(0, stage + 1, node.state_);
            for (Action action : model.actions(stage))
            {
                if (!model.isFeasible(stage, node.state_, action))
                    continue;
                int64_t acc = model.childAcc(stage, node.state_, node.acc_, action);
                double future = model.childFuture(0, stage + 1, node.state_, action, base);
                uint64_t hash = model.childHash(stage, node.state_, node.hash_, action);
                candidates.push_back({acc, acc + future + model.suffixPrice(0, stage + 1), hash,
                                      static_cast<uint16_t>(parent), action});
            }
        }
        if (candidates.empty())
            throw runtime_error("lagbeam has no feasible candidate");

        int preselect = min<int>(candidates.size(), cfg.width_ * cfg.preselect_factor_);
        if ((int)candidates.size() > preselect)
        {
            nth_element(candidates.begin(), candidates.begin() + preselect, candidates.end(),
                        [](const Candidate& a, const Candidate& b)
                        { return betterObjective<SearchModel>(a.eval_, b.eval_); });
            candidates.resize(preselect);
        }

        // 1本目の境界で事前選抜した候補だけ、残りの境界でも評価する。
        // OfastではNaN判定が最適化で消えるため、計算済みかを別配列で管理する。
        // vector<bool>のproxy参照を避けるため、ここだけはboolではなく1byteで持つ。
        vector<vector<double>> cached(max(0, cuts - 1), vector<double>(beam.size()));
        vector<vector<unsigned char>> cached_valid(max(0, cuts - 1),
                                                   vector<unsigned char>(beam.size()));
        for (auto& candidate : candidates)
        {
            const auto& parent = beam[candidate.parent_];
            for (int cut = 1; cut < cuts; cut++)
            {
                double& base = cached[cut - 1][candidate.parent_];
                if (!cached_valid[cut - 1][candidate.parent_])
                {
                    base = model.parentFuture(cut, stage + 1, parent.state_);
                    cached_valid[cut - 1][candidate.parent_] = true;
                }
                double future =
                    model.childFuture(cut, stage + 1, parent.state_, candidate.action_, base);
                candidate.eval_ = tightenBound<SearchModel>(
                    candidate.eval_, candidate.acc_ + future + model.suffixPrice(cut, stage + 1));
            }
        }

        sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
             { return betterObjective<SearchModel>(a.eval_, b.eval_); });

        // 暫定解による枝刈りと、同じハッシュの状態の重複除去を行う。
        ++stamp;
        int kept = 0;
        for (int i = 0; i < (int)candidates.size() && kept < cfg.width_; i++)
        {
            if constexpr (SearchModel::MAXIMIZE)
            {
                if (candidates[i].eval_ + cfg.incumbent_epsilon_ < incumbent)
                    continue;
            }
            else
            {
                if (candidates[i].eval_ - cfg.incumbent_epsilon_ > incumbent)
                    continue;
            }
            uint64_t hash = candidates[i].hash_;
            int slot = static_cast<int>(hash & (cfg.hash_table_size_ - 1));
            while (hash_seen[slot] == stamp && hash_keys[slot] != hash)
                slot = (slot + 1) & (cfg.hash_table_size_ - 1);
            if (hash_seen[slot] == stamp)
                continue;
            hash_seen[slot] = stamp;
            hash_keys[slot] = hash;
            candidates[kept++] = candidates[i];
        }
        if (kept == 0)
        {
            candidates[0] = *max_element(candidates.begin(), candidates.end(),
                                         [](const Candidate& a, const Candidate& b) {
                                             return betterObjective<SearchModel>(b.eval_, a.eval_);
                                         });
            kept = 1;
        }
        candidates.resize(kept);

        // 選ばれた候補だけ状態をコピーし、1手進める。
        next_beam.clear();
        next_beam.resize(kept);
        vector<Trace> trace(kept);
        for (int i = 0; i < kept; i++)
        {
            const auto& candidate = candidates[i];
            auto& next = next_beam[i];
            next.acc_ = candidate.acc_;
            next.eval_ = candidate.eval_;
            next.hash_ = candidate.hash_;
            next.state_ = beam[candidate.parent_].state_;
            model.apply(next.state_, stage, candidate.action_);
            trace[i] = {candidate.parent_, candidate.action_};
        }
        layers.push_back(move(trace));
        beam.swap(next_beam);
    }

    int best_index = 0;
    int64_t best_score = SearchModel::MAXIMIZE ? numeric_limits<int64_t>::lowest()
                                               : numeric_limits<int64_t>::max();
    for (int i = 0; i < (int)beam.size(); i++)
    {
        int64_t score = model.finalScore(beam[i].state_, beam[i].acc_);
        if (betterObjective<SearchModel>(score, best_score))
        {
            best_score = score;
            best_index = i;
        }
    }
    vector<Action> actions(stages);
    for (int stage = stages - 1; stage >= 0; stage--)
    {
        const auto& trace = layers[stage][best_index];
        actions[stage] = trace.action_;
        best_index = trace.parent_;
    }
    Answer answer = model.makeAnswer(actions);
    int64_t answer_score = model.scoreAnswer(answer);
    if (answer_score != best_score)
        throw logic_error("lagbeam finalScore and scoreAnswer mismatch");
    return {answer, answer_score};
}

// 双対価格更新、価格選択、ビームサーチをまとめて実行する外部API。
template <LagBeamProblem Problem>
Result<typename Problem::Answer> solve(const Problem& problem, const Config& cfg = {})
{
    using Answer = typename Problem::Answer;
    Result<Answer> result;
    result.initial_ = problem.initialIncumbent();
    DualResult dual = optimizeDual(problem, result.initial_.score_, cfg.dual_);
    vector<vector<double>> prices = selectPriceCuts<Problem>(dual, cfg.cuts_);
    auto search = problem.prepareSearch(prices);
    using SearchModel = decay_t<decltype(search)>;
    static_assert(LagBeamSearchModel<SearchModel>,
                  "prepareSearch() must return a LagBeamSearchModel");
    static_assert(same_as<typename Problem::Answer, typename SearchModel::Answer>,
                  "Problem and SearchModel must use the same Answer type");
    static_assert(Problem::MAXIMIZE == SearchModel::MAXIMIZE,
                  "Problem and SearchModel objective senses must match");
    result.rounded_ = search.roundedAnswer();
    int64_t incumbent = result.initial_.score_;
    if (betterObjective<Problem>(result.rounded_.score_, incumbent))
        incumbent = result.rounded_.score_;
    result.beam_ = boundedBeamSearch(search, incumbent, cfg.beam_);
    result.best_ = result.initial_;
    if (betterObjective<Problem>(result.rounded_.score_, result.best_.score_))
        result.best_ = result.rounded_;
    if (betterObjective<Problem>(result.beam_.score_, result.best_.score_))
        result.best_ = result.beam_;
    result.dual_bound_ = dual.best_bound_;
    return result;
}

} // namespace lagbeam
