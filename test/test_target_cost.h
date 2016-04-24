#pragma once
#include "picotest/picotest.h"
#include "testhelper.h"
#include "tiny_cnn/tiny_cnn.h"
#include "tiny_cnn/util/target_cost.h"

namespace tiny_cnn {

TEST(target_cost, calculate_label_counts) {
    const std::vector<label_t> t = { 0, 1, 4, 0, 1, 2 }; // note that there's no class "3"

    const std::vector<cnn_size_t> label_counts = calculate_label_counts(t);

    EXPECT_EQ(label_counts.size(), 5);
    EXPECT_EQ(label_counts[0], 2);
    EXPECT_EQ(label_counts[1], 2);
    EXPECT_EQ(label_counts[2], 1);
    EXPECT_EQ(label_counts[3], 0);
    EXPECT_EQ(label_counts[4], 1);
}

TEST(target_cost, get_sample_weight_for_balanced_target_cost) {
    const std::vector<cnn_size_t> class_sample_counts = { 1000, 100, 10, 1 };
    const cnn_size_t class_count = class_sample_counts.size();
    const cnn_size_t total_samples = std::accumulate(class_sample_counts.begin(), class_sample_counts.end(), static_cast<cnn_size_t>(0));

    std::vector<float_t> class_weights;
    for (cnn_size_t class_sample_count : class_sample_counts) {
        class_weights.push_back(get_sample_weight_for_balanced_target_cost(class_count, total_samples, class_sample_count));
    }

    EXPECT_EQ(class_weights.size(), class_sample_counts.size());

    EXPECT_NEAR(class_weights[0], 0.27775, 1e-6);
    EXPECT_NEAR(class_weights[1], 2.7775, 1e-6);
    EXPECT_NEAR(class_weights[2], 27.775, 1e-6);
    EXPECT_NEAR(class_weights[3], 277.75, 1e-6);

    const float_t expected_product = class_weights[0] * class_sample_counts[0];
    float_t sum_of_products = expected_product;
    for (size_t i = 1; i < class_sample_counts.size(); ++i) {
        const float_t actual_product = class_weights[i] * class_sample_counts[i];
        EXPECT_NEAR(expected_product, actual_product, 1e-6);
        sum_of_products += actual_product;
    }

    EXPECT_NEAR(sum_of_products, total_samples, 1e-6);
}

TEST(target_cost, create_balanced_target_cost_0) {
    const float_t w = 0;

    const std::vector<label_t> t = { 0, 1, 4, 0, 1, 2 }; // note that there's no class "3"

    const auto target_cost = create_balanced_target_cost(t, w);

    EXPECT_EQ(target_cost.size(), t.size());

    for (size_t sample = 0; sample < t.size(); ++sample) {
        const vec_t& sample_cost = target_cost[sample];
        EXPECT_EQ(sample_cost.size(), 5);

        for (size_t i = 0; i < sample_cost.size(); ++i) {
            EXPECT_NEAR(sample_cost[i], 1.0, 1e-6);
        }
    }
}

TEST(target_cost, create_balanced_target_cost_1) {
    const float_t w = 1;

    const std::vector<label_t> t = { 0, 1, 4, 0, 1, 2 }; // note that there's no class "3"

    const auto target_cost = create_balanced_target_cost(t, w);

    const std::vector<cnn_size_t> label_counts = calculate_label_counts(t);

    EXPECT_EQ(target_cost.size(), t.size());

    for (size_t sample = 0; sample < t.size(); ++sample) {
        const vec_t& sample_cost = target_cost[sample];
        EXPECT_EQ(sample_cost.size(), 5);
        EXPECT_GE(label_counts[t[sample]], 1);

        float_t expected_weight = t.size() / static_cast<double>(label_counts.size() * label_counts[t[sample]]);

        for (size_t label = 0; label < sample_cost.size(); ++label) {
            EXPECT_NEAR(sample_cost[label], expected_weight, 1e-6);
        }
    }
}

TEST(target_cost, create_balanced_target_cost_0_5) {
    const float_t w = 0.5;

    const std::vector<label_t> t = { 0, 1, 4, 0, 1, 2 }; // note that there's no class "3"

    const auto target_cost = create_balanced_target_cost(t, w);

    const std::vector<cnn_size_t> label_counts = calculate_label_counts(t);

    EXPECT_EQ(target_cost.size(), t.size());

    for (size_t sample = 0; sample < t.size(); ++sample) {
        const vec_t& sample_cost = target_cost[sample];
        EXPECT_EQ(sample_cost.size(), 5);
        EXPECT_GE(label_counts[t[sample]], 1);

        const float_t expected_weight_w_0 = 1;
        const float_t expected_weight_w_1 = t.size() / static_cast<double>(label_counts.size() * label_counts[t[sample]]);

        for (size_t label = 0; label < sample_cost.size(); ++label) {
            const float_t label_cost = sample_cost[label];
            if (expected_weight_w_1 > 1) {
                EXPECT_GE(label_cost, expected_weight_w_0);
                EXPECT_LE(label_cost, expected_weight_w_1);
            }
            else if (expected_weight_w_1 < 1) {
                EXPECT_LE(label_cost, expected_weight_w_0);
                EXPECT_GE(label_cost, expected_weight_w_1);
            }
            else {
                EXPECT_NEAR(label_cost, 1.0, 1e-6);
            }

            EXPECT_NEAR(label_cost, (1 - w) * expected_weight_w_0 + w * expected_weight_w_1, 1e-6);
        }
    }
}

TEST(target_cost, train_unbalanced_data_1dim) {
    // train a really simple function with noisy, unbalanced training data:
    // 1) assuming equal cost for each training sample, in which case the total cost
    //    (error) is rightly minimized by always guessing the majority class (1), and
    // 2) assuming equal cost for each class, in which case the "true" function
    //    (identity) can be learned

    const float_t p = 0.9;  // p(in == 1)
    const float_t p0 = 0.6; // p(label == 1 | in == 0)
    const float_t p1 = 0.9; // p(label == 1 | in == 1)

    auto create_net = []() {
        network<mse, adagrad> net;
        net << fully_connected_layer<tan_h>(1, 10)
            << fully_connected_layer<tan_h>(10, 2);
        return net;
    };

    network<mse, adagrad> net_equal_sample_cost = create_net();
    network<mse, adagrad> net_equal_class_cost  = create_net();

    std::vector<vec_t> data;
    std::vector<label_t> labels;
    const size_t tnum = 1000;

    for (size_t i = 0; i < tnum; i++) {
        bool in = bernoulli(p);
        bool label = in ? bernoulli(p1) : bernoulli(p0);

        data.push_back({ in * 1.0 });
        labels.push_back(label ? 1 : 0);
    }

    { // some simple checks
        const float_t p_label1 = p0 * (1 - p) + p1 * p; // p(label == 1) = p(label == 1 | in == 0) * p(in == 0) + p(label == 1 | in == 1) * p(in == 1)
        const cnn_size_t n_label1 = std::accumulate(labels.begin(), labels.end(), static_cast<cnn_size_t>(0));

        EXPECT_NEAR(n_label1 / static_cast<float_t>(tnum), p_label1, 0.05);
        EXPECT_GE(n_label1, 800);
        EXPECT_LE(n_label1, 900);
    }

    const auto balanced_cost = create_balanced_target_cost(labels); // give higher weight to samples in the minority class

    // train both networks - one with implicit cost (equal for each sample),
    // and the other with explicit cost (balanced, or equal for each class)
    net_equal_sample_cost.train(data, labels, 10, 100, nop, nop, true, CNN_TASK_SIZE, NULL);
    net_equal_class_cost .train(data, labels, 10, 100, nop, nop, true, CNN_TASK_SIZE, &balanced_cost);

    // count errors
    size_t errors_equal_sample_cost = 0;
    size_t errors_equal_class_cost  = 0;

    for (size_t i = 0; i < tnum; i++) {
        // the test data is balanced between the classes
        const bool in = bernoulli(0.5);
        const label_t expected = in ? 1 : 0;
        const vec_t input = { in * 1.0 };
        const label_t actual_equal_sample_cost = net_equal_sample_cost.predict_label(input);
        const label_t actual_equal_class_cost  = net_equal_class_cost .predict_label(input);

        // the first net always guesses the majority class
        EXPECT_EQ(actual_equal_sample_cost, 1);

        // count errors
        errors_equal_sample_cost += (expected == actual_equal_sample_cost) ? 0 : 1;
        errors_equal_class_cost  += (expected == actual_equal_class_cost)  ? 0 : 1;
    }

    EXPECT_GE(errors_equal_sample_cost, 0.25 * tnum); // should have plenty of errors
    EXPECT_EQ(errors_equal_class_cost,  0); // should have learned the desired function
}

TEST(target_cost, train_unbalanced_data) {
    // train xor function with noisy, unbalanced training data:
    // 1) assuming equal cost for each training sample, in which case the total cost
    //    (error) is rightly minimized by always guessing the majority class (1), and
    // 2) assuming equal cost for each class, in which case the correct underlying
    //    function can be learned.

    const float_t p = 0.9; // p(label == 1)
    const float_t noise = 0.25;

    auto create_net = []() {
        network<mse, adagrad> net;
        net << fully_connected_layer<tan_h>(2, 10)
            << fully_connected_layer<tan_h>(10, 2);
        return net;
    };

    network<mse, adagrad> net_equal_sample_cost = create_net();
    network<mse, adagrad> net_equal_class_cost  = create_net();

    std::vector<vec_t> data;
    std::vector<label_t> labels;
    const size_t tnum = 2000;

    for (size_t i = 0; i < tnum; i++) {
        const bool label = bernoulli(p);

        bool in0 = bernoulli(0.5);
        bool in1 = (in0 ^ label);

        // add noise to make things more interesting
        if (bernoulli(noise)) {
            in1 = !in1;
        }

        data.push_back({ in0 * 1.0, in1 * 1.0 });
        labels.push_back(label ? 1 : 0);
    }

    const auto balanced_cost = create_balanced_target_cost(labels); // give higher weight to samples in the minority class

    // train both networks - one with implicit cost (equal for each sample),
    // and the other with explicit cost (balanced, or equal for each class)
    net_equal_sample_cost.train(data, labels, 10, 100, nop, nop, true, CNN_TASK_SIZE, NULL);
    net_equal_class_cost .train(data, labels, 10, 100, nop, nop, true, CNN_TASK_SIZE, &balanced_cost);

    // count errors
    size_t errors_equal_sample_cost = 0;
    size_t errors_equal_class_cost  = 0;

    for (size_t i = 0; i < tnum; i++) {
        // the test data is balanced between the classes
        const bool in[2] = { bernoulli(0.5), bernoulli(0.5) };
        const label_t expected = (in[0] ^ in[1]) ? 1 : 0;
        const vec_t input = { in[0] * 1.0, in[1] * 1.0 };
        const label_t actual_equal_sample_cost = net_equal_sample_cost.predict_label(input);
        const label_t actual_equal_class_cost  = net_equal_class_cost .predict_label(input);

        // the first net always guesses the majority class
        EXPECT_EQ(actual_equal_sample_cost, 1);

        // count errors
        errors_equal_sample_cost += (expected == actual_equal_sample_cost) ? 0 : 1;
        errors_equal_class_cost  += (expected == actual_equal_class_cost)  ? 0 : 1;
    }

    EXPECT_GE(errors_equal_sample_cost, 0.25 * tnum); // should have plenty of errors
    EXPECT_EQ(errors_equal_class_cost,  0); // should have learned the desired function
}

} // namespace tiny-cnn