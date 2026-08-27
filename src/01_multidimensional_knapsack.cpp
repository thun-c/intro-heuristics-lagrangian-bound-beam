#pragma GCC optimize("Ofast")
#include <bits/stdc++.h>
using namespace std;

#include "lib/lagbeam.hpp"

/*************************************************************************
 * 多次元0/1ナップサック固有部分。
 *
 * 入力:
 *   品物数N 資源数M
 *   各資源の容量M個
 *   各品物について 価値 資源使用量M個
 *************************************************************************/
namespace multidimensional_knapsack
{

int item_count;
int resource_count;
vector<int64_t> capacity;
vector<int64_t> profit;
vector<vector<int64_t>> consumption;

void input()
{
    cin >> item_count >> resource_count;
    capacity.resize(resource_count);
    for (int64_t& value : capacity)
        cin >> value;
    profit.resize(item_count);
    consumption.assign(item_count, vector<int64_t>(resource_count));
    for (int item = 0; item < item_count; item++)
    {
        cin >> profit[item];
        for (int64_t& value : consumption[item])
            cin >> value;
    }
}

int64_t score(const vector<int>& answer)
{
    int64_t value = 0;
    for (int item = 0; item < item_count; item++)
        if (answer[item])
            value += profit[item];
    return value;
}

bool isFeasibleAnswer(const vector<int>& answer)
{
    vector<int64_t> used(resource_count);
    for (int item = 0; item < item_count; item++)
    {
        if (!answer[item])
            continue;
        for (int resource = 0; resource < resource_count; resource++)
            used[resource] += consumption[item][resource];
    }
    for (int resource = 0; resource < resource_count; resource++)
        if (used[resource] > capacity[resource])
            return false;
    return true;
}

// 価値を、容量に対する資源使用率で割った単純な貪欲解を作る。
vector<int> makeGreedyAnswer()
{
    vector<int> order(item_count);
    iota(order.begin(), order.end(), 0);
    vector<double> density(item_count);
    for (int item = 0; item < item_count; item++)
    {
        double normalized_usage = 0.0;
        for (int resource = 0; resource < resource_count; resource++)
        {
            if (capacity[resource] > 0)
            {
                normalized_usage +=
                    static_cast<double>(consumption[item][resource]) / capacity[resource];
            }
        }
        density[item] = profit[item] / max(1e-12, normalized_usage);
    }
    sort(order.begin(), order.end(), [&](int a, int b) {
        if (density[a] != density[b])
            return density[a] > density[b];
        return a < b;
    });

    vector<int> answer(item_count);
    vector<int64_t> used(resource_count);
    for (int item : order)
    {
        bool feasible = true;
        for (int resource = 0; resource < resource_count; resource++)
        {
            if (used[resource] + consumption[item][resource] > capacity[resource])
                feasible = false;
        }
        if (!feasible)
            continue;
        answer[item] = 1;
        for (int resource = 0; resource < resource_count; resource++)
            used[resource] += consumption[item][resource];
    }
    return answer;
}

void output(const lagbeam::Result<vector<int>>& result)
{
    cout << "Best score: " << result.best_.score_ << endl;
    cout << "Selected items:";
    for (int item = 0; item < item_count; item++)
        if (result.best_.answer_[item])
            cout << ' ' << item + 1;
    cout << endl;
    cout << "Greedy score: " << result.initial_.score_ << endl;
    cout << "Dual upper bound: " << fixed << setprecision(3) << result.dual_bound_ << endl;
}

static inline uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

struct SearchModel
{
    using Answer = vector<int>;
    using Action = unsigned char;
    static constexpr bool MAXIMIZE = true;
    struct State
    {
        vector<int64_t> used_;
    };

    vector<vector<double>> prices_;
    vector<vector<double>> suffix_reduced_profit_;
    vector<double> dual_constant_;
    vector<uint64_t> hash_coefficient_;
    vector<Action> actions_{0, 1};
    lagbeam::ScoredAnswer<Answer> rounded_;

    explicit SearchModel(vector<vector<double>> selected_prices)
        : prices_(move(selected_prices))
    {
        suffix_reduced_profit_.assign(prices_.size(), vector<double>(item_count + 1));
        dual_constant_.resize(prices_.size());
        for (int cut = 0; cut < (int)prices_.size(); cut++)
        {
            for (int resource = 0; resource < resource_count; resource++)
                dual_constant_[cut] += prices_[cut][resource] * capacity[resource];
            for (int item = item_count - 1; item >= 0; item--)
            {
                double reduced_profit = profit[item];
                for (int resource = 0; resource < resource_count; resource++)
                    reduced_profit -= prices_[cut][resource] * consumption[item][resource];
                suffix_reduced_profit_[cut][item] =
                    suffix_reduced_profit_[cut][item + 1] + max(0.0, reduced_profit);
            }
        }
        hash_coefficient_.resize(resource_count);
        for (int resource = 0; resource < resource_count; resource++)
            hash_coefficient_[resource] = mix64(1234567ULL + resource);

        Answer greedy = makeGreedyAnswer();
        rounded_ = {greedy, score(greedy)};
    }

    int stageCount() const
    {
        return item_count;
    }
    int cutCount() const
    {
        return prices_.size();
    }
    int maxActions() const
    {
        return 2;
    }
    State initialState() const
    {
        return {vector<int64_t>(resource_count)};
    }
    uint64_t initialHash() const
    {
        return 0;
    }
    const vector<Action>& actions(int) const
    {
        return actions_;
    }
    bool isFeasible(int stage, const State& state, Action action) const
    {
        if (!action)
            return true;
        for (int resource = 0; resource < resource_count; resource++)
        {
            if (state.used_[resource] + consumption[stage][resource] > capacity[resource])
                return false;
        }
        return true;
    }
    int64_t childAcc(int stage, const State&, int64_t acc, Action action) const
    {
        return acc + action * profit[stage];
    }
    uint64_t childHash(int stage, const State&, uint64_t hash, Action action) const
    {
        if (!action)
            return hash;
        for (int resource = 0; resource < resource_count; resource++)
        {
            hash += hash_coefficient_[resource] *
                    static_cast<uint64_t>(consumption[stage][resource]);
        }
        return hash;
    }
    void apply(State& state, int stage, Action action) const
    {
        if (!action)
            return;
        for (int resource = 0; resource < resource_count; resource++)
            state.used_[resource] += consumption[stage][resource];
    }
    double parentFuture(int cut, int, const State& state) const
    {
        double used_price = 0.0;
        for (int resource = 0; resource < resource_count; resource++)
            used_price += prices_[cut][resource] * state.used_[resource];
        return -used_price;
    }
    double childFuture(int cut, int next_stage, const State&, Action action, double parent) const
    {
        if (!action)
            return parent;
        int item = next_stage - 1;
        for (int resource = 0; resource < resource_count; resource++)
            parent -= prices_[cut][resource] * consumption[item][resource];
        return parent;
    }
    double suffixPrice(int cut, int next_stage) const
    {
        return dual_constant_[cut] + suffix_reduced_profit_[cut][next_stage];
    }
    int64_t finalScore(const State&, int64_t acc) const
    {
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
        return score(answer);
    }
};

struct Problem
{
    using Answer = vector<int>;
    using DualActivity = vector<double>;
    static constexpr bool MAXIMIZE = true;

    int dualDimension() const
    {
        return resource_count;
    }
    vector<double> initialDualPrices() const
    {
        return vector<double>(resource_count);
    }
    DualActivity makeDualActivity() const
    {
        return DualActivity(resource_count);
    }
    int relaxedBlockCount() const
    {
        return item_count;
    }
    lagbeam::ScoredAnswer<Answer> initialIncumbent() const
    {
        Answer greedy = makeGreedyAnswer();
        return {greedy, score(greedy)};
    }
    double dualConstant(const vector<double>& price) const
    {
        double value = 0.0;
        for (int resource = 0; resource < resource_count; resource++)
            value += price[resource] * capacity[resource];
        return value;
    }
    double relaxedBlockValue(int item, const vector<double>& price,
                             DualActivity* activity) const
    {
        double reduced_profit = profit[item];
        for (int resource = 0; resource < resource_count; resource++)
            reduced_profit -= price[resource] * consumption[item][resource];
        if (reduced_profit <= 0.0)
            return 0.0;
        if (activity)
        {
            for (int resource = 0; resource < resource_count; resource++)
                (*activity)[resource] += consumption[item][resource];
        }
        return reduced_profit;
    }
    void computeDualImprovementDirection(const DualActivity& activity,
                                         vector<double>& direction) const
    {
        for (int resource = 0; resource < resource_count; resource++)
            direction[resource] = activity[resource] - capacity[resource];
    }
    void projectDualPrices(vector<double>& price) const
    {
        for (double& value : price)
            value = max(0.0, value);
    }
    SearchModel prepareSearch(const vector<vector<double>>& selected_prices) const
    {
        return SearchModel(selected_prices);
    }
};

} // namespace multidimensional_knapsack

int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    multidimensional_knapsack::input();
    multidimensional_knapsack::Problem problem;
    lagbeam::Config config;
    config.dual_.iterations_ = 60;
    config.dual_.min_step_ = 0.01;
    config.dual_.initial_step_cap_ = 10.0;
    config.dual_.final_step_cap_ = 1.0;
    config.cuts_.count_ = 3;
    config.beam_.width_ = 2000;
    config.beam_.preselect_factor_ = 2;
    config.beam_.hash_table_size_ = 1 << 13;

    auto result = lagbeam::solve(problem, config);
    multidimensional_knapsack::output(result);
    return 0;
}
