// この提出コードのライブラリとサンプルコードを以下に格納しています。
// https://github.com/thun-c/intro-heuristics-lagrangian-bound-beam
#include "lib/lagbeam.hpp"

/*************************************************************************
 * Intro Heuristics固有部分。
 * 同じlagbeam.hppを編集せず、ProblemとSearchModelだけを実装する。
 *************************************************************************/
namespace intro_heuristics
{

constexpr int TYPES = 26;
int days;
array<int, TYPES> penalty;
vector<array<int, TYPES>> satisfaction;

void input()
{
    cin >> days;
    for (int& value : penalty)
        cin >> value;
    satisfaction.resize(days);
    for (auto& row : satisfaction)
        for (int& value : row)
            cin >> value;
}

void output(const vector<int>& answer)
{
    for (int type : answer)
        cout << type + 1 << endl;
}

static inline int64_t triangular(int n)
{
    return 1LL * n * (n + 1) / 2;
}

static inline uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

int64_t calculateScore(const vector<int>& answer)
{
    array<int, TYPES> last{};
    int64_t score = 0;
    for (int day = 0; day < days; day++)
    {
        int type = answer[day];
        last[type] = day + 1;
        score += satisfaction[day][type];
        for (int t = 0; t < TYPES; t++)
            score -= 1LL * penalty[t] * (day + 1 - last[t]);
    }
    return score;
}

struct SearchModel
{
    using Answer = vector<int>;
    using Action = unsigned char;
    static constexpr bool MAXIMIZE = true;
    struct State
    {
        array<short, TYPES> last_{};
    };

    vector<vector<Action>> actions_;
    vector<vector<vector<double>>> future_;
    vector<vector<double>> suffix_price_;
    vector<vector<uint64_t>> zobrist_;
    lagbeam::ScoredAnswer<Answer> rounded_;
    int max_actions_ = 0;

    explicit SearchModel(const vector<vector<double>>& prices)
    {
        assert(!prices.empty());
        vector<array<double, TYPES>> guide = buildGuide(prices[0]);
        buildRoundedAnswer(guide);
        buildActions(guide);
        buildFuture(prices);
        buildZobrist();
    }

    int futureId(int stage, int last_plus_one) const
    {
        return stage * (days + 1) + last_plus_one;
    }

    vector<array<double, TYPES>> buildGuide(const vector<double>& price) const
    {
        // 最良価格に対する種類別DPから、各日の候補種類を評価する。
        vector<array<double, TYPES>> guide(days);
        for (int t = 0; t < TYPES; t++)
        {
            vector<double> dp(days), tail(days);
            const double c = penalty[t];
            for (int day = 0; day < days; day++)
            {
                double best_previous = -c * triangular(day);
                for (int last = 0; last < day; last++)
                    best_previous = max(best_previous, dp[last] - c * triangular(day - last - 1));
                dp[day] = best_previous + satisfaction[day][t] - price[day];
            }
            for (int day = days - 1; day >= 0; day--)
            {
                double best_tail = -c * triangular(days - 1 - day);
                for (int right = day + 1; right < days; right++)
                {
                    double value = satisfaction[right][t] - price[right] -
                                   c * triangular(right - day - 1) + tail[right];
                    best_tail = max(best_tail, value);
                }
                tail[day] = best_tail;
            }
            double best = -c * triangular(days);
            for (int day = 0; day < days; day++)
                best = max(best, dp[day] - c * triangular(days - 1 - day));
            for (int day = 0; day < days; day++)
                guide[day][t] = dp[day] + tail[day] - best;
        }
        return guide;
    }

    void buildRoundedAnswer(const vector<array<double, TYPES>>& guide)
    {
        Answer answer(days);
        for (int day = 0; day < days; day++)
        {
            int best_type = 0;
            for (int t = 1; t < TYPES; t++)
                if (guide[day][t] > guide[day][best_type])
                    best_type = t;
            answer[day] = best_type;
        }
        rounded_ = {answer, calculateScore(answer)};
    }

    void buildActions(const vector<array<double, TYPES>>& guide)
    {
        // 評価が近い上位6～10種類だけをビームで展開する。
        actions_.resize(days);
        for (int day = 0; day < days; day++)
        {
            array<int, TYPES> order{};
            iota(order.begin(), order.end(), 0);
            sort(order.begin(), order.end(),
                 [&](int a, int b) { return guide[day][a] > guide[day][b]; });
            int count = 0;
            while (count < TYPES && guide[day][order[count]] >= guide[day][order[0]] - 5000.0)
                count++;
            count = clamp(count, 6, 10);
            actions_[day].resize(count);
            for (int i = 0; i < count; i++)
                actions_[day][i] = order[i];
            max_actions_ = max(max_actions_, count);
        }
    }

    void buildFuture(const vector<vector<double>>& prices)
    {
        const int table_size = (days + 1) * (days + 1);
        // 一時的に巨大な3次元配列を複製しないよう、価格ごとに確保する。
        future_.resize(prices.size());
        for (auto& cut_future : future_)
            cut_future.assign(TYPES, vector<double>(table_size, -1e100));
        suffix_price_.assign(prices.size(), vector<double>(days + 1));

        // lastと開始日から、各種類が残り期間で得られる最大値を前計算する。
        for (int cut = 0; cut < (int)prices.size(); cut++)
        {
            auto& future = future_[cut];
            const auto& price = prices[cut];
            for (int t = 0; t < TYPES; t++)
            {
                for (int last_plus_one = 0; last_plus_one <= days; last_plus_one++)
                {
                    int last = last_plus_one - 1;
                    if (last < days)
                        future[t][futureId(days, last_plus_one)] =
                            -1.0 * penalty[t] * triangular(days - last - 1);
                }
                for (int stage = days - 1; stage >= 0; stage--)
                {
                    for (int last_plus_one = 0; last_plus_one <= stage; last_plus_one++)
                    {
                        int last = last_plus_one - 1;
                        double skip = future[t][futureId(stage + 1, last_plus_one)];
                        double take = satisfaction[stage][t] - price[stage] -
                                      1.0 * penalty[t] * triangular(stage - last - 1) +
                                      future[t][futureId(stage + 1, stage + 1)];
                        future[t][futureId(stage, last_plus_one)] = max(skip, take);
                    }
                }
            }
            for (int day = days - 1; day >= 0; day--)
                suffix_price_[cut][day] = suffix_price_[cut][day + 1] + price[day];
        }
    }

    void buildZobrist()
    {
        zobrist_.assign(TYPES, vector<uint64_t>(days + 1));
        for (int t = 0; t < TYPES; t++)
            for (int last = 0; last <= days; last++)
                zobrist_[t][last] = mix64(1234567ULL + 1ULL * t * (days + 1) + last);
    }

    int stageCount() const
    {
        return days;
    }
    int cutCount() const
    {
        return future_.size();
    }
    int maxActions() const
    {
        return max_actions_;
    }
    State initialState() const
    {
        return {};
    }
    uint64_t initialHash() const
    {
        uint64_t hash = 0;
        for (int t = 0; t < TYPES; t++)
            hash ^= zobrist_[t][0];
        return hash;
    }
    const vector<Action>& actions(int stage) const
    {
        return actions_[stage];
    }
    bool isFeasible(int, const State&, Action) const
    {
        // どの日も、候補に入れた全種類を必ず開催できる。
        return true;
    }
    int64_t childAcc(int stage, const State& state, int64_t acc, Action action) const
    {
        int type = action;
        int last = state.last_[type] - 1;
        return acc + satisfaction[stage][type] -
               1LL * penalty[type] * triangular(stage - last - 1);
    }
    uint64_t childHash(int stage, const State& state, uint64_t hash, Action action) const
    {
        int type = action;
        return hash ^ zobrist_[type][state.last_[type]] ^ zobrist_[type][stage + 1];
    }
    void apply(State& state, int stage, Action action) const
    {
        state.last_[action] = stage + 1;
    }
    double parentFuture(int cut, int next_stage, const State& state) const
    {
        double sum = 0.0;
        for (int t = 0; t < TYPES; t++)
            sum += future_[cut][t][futureId(next_stage, state.last_[t])];
        return sum;
    }
    double childFuture(int cut, int next_stage, const State& state, Action action,
                       double parent) const
    {
        int type = action;
        return parent - future_[cut][type][futureId(next_stage, state.last_[type])] +
               future_[cut][type][futureId(next_stage, next_stage)];
    }
    double suffixPrice(int cut, int next_stage) const
    {
        return suffix_price_[cut][next_stage];
    }
    int64_t finalScore(const State& state, int64_t acc) const
    {
        for (int t = 0; t < TYPES; t++)
        {
            int last = state.last_[t] - 1;
            acc -= 1LL * penalty[t] * triangular(days - last - 1);
        }
        return acc;
    }
    lagbeam::ScoredAnswer<Answer> roundedAnswer() const
    {
        return rounded_;
    }
    Answer makeAnswer(const vector<Action>& actions) const
    {
        return Answer(actions.begin(), actions.end());
    }
    int64_t scoreAnswer(const Answer& answer) const
    {
        return calculateScore(answer);
    }
};

struct Problem
{
    using Answer = vector<int>;
    using DualActivity = vector<int>;
    static constexpr bool MAXIMIZE = true;

    int dualDimension() const
    {
        return days;
    }
    vector<double> initialDualPrices() const
    {
        return vector<double>(days, 0.0);
    }
    DualActivity makeDualActivity() const
    {
        return DualActivity(days, 0);
    }
    double dualConstant(const vector<double>& price) const
    {
        return accumulate(price.begin(), price.end(), 0.0);
    }
    void computeDualImprovementDirection(const DualActivity& activity,
                                         vector<double>& direction) const
    {
        for (int day = 0; day < days; day++)
            direction[day] = activity[day] - 1;
    }
    void projectDualPrices(vector<double>&) const
    {
        // 等式制約なのでラグランジュ乗数の符号を制限しない。
    }
    int relaxedBlockCount() const
    {
        return TYPES;
    }

    lagbeam::ScoredAnswer<Answer> initialIncumbent() const
    {
        // その日の満足度と、開催によって解消する不満だけを見る貪欲解。
        Answer greedy(days);
        array<int, TYPES> last{};
        for (int day = 0; day < days; day++)
        {
            int best_type = 0;
            int best_value = INT_MIN;
            for (int t = 0; t < TYPES; t++)
            {
                int value = satisfaction[day][t] + penalty[t] * (day + 1 - last[t]);
                if (value > best_value)
                {
                    best_value = value;
                    best_type = t;
                }
            }
            greedy[day] = best_type;
            last[best_type] = day + 1;
        }
        return {greedy, calculateScore(greedy)};
    }

    double relaxedBlockValue(int type, const vector<double>& price, vector<int>* usage) const
    {
        // 1種類だけを取り出し、開催日の列をDPで最適化する。
        vector<double> dp(days);
        vector<int> parent(days, -1);
        const double c = penalty[type];
        for (int day = 0; day < days; day++)
        {
            double best_previous = -c * triangular(day);
            int best_parent = -1;
            for (int last = 0; last < day; last++)
            {
                double value = dp[last] - c * triangular(day - last - 1);
                if (value > best_previous)
                {
                    best_previous = value;
                    best_parent = last;
                }
            }
            dp[day] = best_previous + satisfaction[day][type] - price[day];
            parent[day] = best_parent;
        }
        double best = -c * triangular(days);
        int end = -1;
        for (int day = 0; day < days; day++)
        {
            double value = dp[day] - c * triangular(days - 1 - day);
            if (value > best)
            {
                best = value;
                end = day;
            }
        }
        if (usage)
            for (int day = end; day != -1; day = parent[day])
                (*usage)[day]++;
        return best;
    }

    SearchModel prepareSearch(const vector<vector<double>>& selected_prices) const
    {
        return SearchModel(selected_prices);
    }
};

} // namespace intro_heuristics

int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    intro_heuristics::input();
    intro_heuristics::Problem problem;
    lagbeam::Config config;
    config.dual_.iterations_ = 60;
    config.cuts_.count_ = 3;
    config.cuts_.candidate_pool_ = 14;
    config.beam_.width_ = 14500;
    config.beam_.preselect_factor_ = 2;
    config.beam_.hash_table_size_ = 1 << 15;

    auto result = lagbeam::solve(problem, config);
    intro_heuristics::output(result.best_.answer_);
    return 0;
}
