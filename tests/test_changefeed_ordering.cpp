// Test: Changefeed Event Ordering Guarantees
// Validates that changefeed maintains correct event ordering per key and globally

#include <gtest/gtest.h>
#include "cdc/changefeed.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>
#include <thread>
#include <vector>
#include <algorithm>
#include <set>

using namespace themis;

class ChangefeedOrderingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any existing test database
        test_db_path_ = "./data/themis_changefeed_ordering_test";
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
        
        // Create RocksDB wrapper
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 128;
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        
        // Create changefeed
        changefeed_ = std::make_unique<Changefeed>(db_->getDB(), nullptr);
    }
    
    void TearDown() override {
        changefeed_.reset();
        db_->close();
        db_.reset();
        
        // Clean up test database
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
    }
    
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<Changefeed> changefeed_;
};

// ===== Sequential Ordering Tests =====

TEST_F(ChangefeedOrderingTest, SequentialEventsHaveIncreasingSequence) {
    // Record multiple events sequentially
    std::vector<uint64_t> sequences;
    
    for (int i = 0; i < 100; i++) {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_PUT;
        event.key = "key_" + std::to_string(i);
        event.value = "{\"index\":" + std::to_string(i) + "}";
        
        auto recorded = changefeed_->recordEvent(event);
        sequences.push_back(recorded.sequence);
    }
    
    // Verify sequences are strictly increasing
    for (size_t i = 1; i < sequences.size(); i++) {
        EXPECT_GT(sequences[i], sequences[i-1]) 
            << "Sequence not increasing at index " << i;
    }
}

TEST_F(ChangefeedOrderingTest, PerKeyOrdering) {
    // Record multiple events for the same key
    const std::string key = "user_1";
    std::vector<Changefeed::ChangeEvent> recorded_events;
    
    // Initial PUT
    {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_PUT;
        event.key = key;
        event.value = "{\"version\":1}";
        recorded_events.push_back(changefeed_->recordEvent(event));
    }
    
    // Update
    {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_PUT;
        event.key = key;
        event.value = "{\"version\":2}";
        recorded_events.push_back(changefeed_->recordEvent(event));
    }
    
    // Delete
    {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_DELETE;
        event.key = key;
        recorded_events.push_back(changefeed_->recordEvent(event));
    }
    
    // Re-create
    {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_PUT;
        event.key = key;
        event.value = "{\"version\":3}";
        recorded_events.push_back(changefeed_->recordEvent(event));
    }
    
    // Verify all events for this key are in order
    for (size_t i = 1; i < recorded_events.size(); i++) {
        EXPECT_GT(recorded_events[i].sequence, recorded_events[i-1].sequence);
        EXPECT_EQ(recorded_events[i].key, key);
    }
    
    // Query all events and verify order
    Changefeed::ListOptions options;
    options.start_sequence = 0;
    options.limit = 100;
    
    auto events = changefeed_->listEvents(options);
    
    // Filter events for our key
    std::vector<Changefeed::ChangeEvent> key_events;
    for (const auto& evt : events) {
        if (evt.key == key) {
            key_events.push_back(evt);
        }
    }
    
    ASSERT_EQ(key_events.size(), 4);
    EXPECT_EQ(key_events[0].type, Changefeed::ChangeEventType::EVENT_PUT);
    EXPECT_EQ(key_events[1].type, Changefeed::ChangeEventType::EVENT_PUT);
    EXPECT_EQ(key_events[2].type, Changefeed::ChangeEventType::EVENT_DELETE);
    EXPECT_EQ(key_events[3].type, Changefeed::ChangeEventType::EVENT_PUT);
}

// ===== Concurrent Write Ordering Tests =====

TEST_F(ChangefeedOrderingTest, ConcurrentWritesHaveUniqueSequences) {
    const int num_threads = 10;
    const int events_per_thread = 100;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<uint64_t>> thread_sequences(num_threads);
    
    // Each thread records events
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < events_per_thread; i++) {
                Changefeed::ChangeEvent event;
                event.type = Changefeed::ChangeEventType::EVENT_PUT;
                event.key = "thread_" + std::to_string(t) + "_key_" + std::to_string(i);
                event.value = "{\"thread\":" + std::to_string(t) + ",\"index\":" + std::to_string(i) + "}";
                
                auto recorded = changefeed_->recordEvent(event);
                thread_sequences[t].push_back(recorded.sequence);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Collect all sequences
    std::set<uint64_t> all_sequences;
    for (const auto& thread_seqs : thread_sequences) {
        for (uint64_t seq : thread_seqs) {
            all_sequences.insert(seq);
        }
    }
    
    // Verify all sequences are unique
    size_t total_events = num_threads * events_per_thread;
    EXPECT_EQ(all_sequences.size(), total_events) 
        << "Duplicate sequences detected!";
}

// ===== Global Ordering Tests =====

TEST_F(ChangefeedOrderingTest, GlobalOrderingWithMultipleKeys) {
    // Record events for multiple keys in a specific order
    std::vector<std::string> keys = {"user_1", "user_2", "user_3"};
    std::vector<Changefeed::ChangeEvent> recorded_events;
    
    for (int iteration = 0; iteration < 10; iteration++) {
        for (const auto& key : keys) {
            Changefeed::ChangeEvent event;
            event.type = Changefeed::ChangeEventType::EVENT_PUT;
            event.key = key;
            event.value = "{\"iteration\":" + std::to_string(iteration) + "}";
            
            recorded_events.push_back(changefeed_->recordEvent(event));
        }
    }
    
    // Query all events
    Changefeed::ListOptions options;
    options.start_sequence = 0;
    options.limit = 1000;
    
    auto events = changefeed_->listEvents(options);
    
    ASSERT_EQ(events.size(), recorded_events.size());
    
    // Verify global ordering matches recorded order
    for (size_t i = 0; i < events.size(); i++) {
        EXPECT_EQ(events[i].sequence, recorded_events[i].sequence);
        EXPECT_EQ(events[i].key, recorded_events[i].key);
    }
}

// ===== Pagination Ordering Tests =====

TEST_F(ChangefeedOrderingTest, PaginationPreservesOrdering) {
    // Record 1000 events
    const int total_events = 1000;
    std::vector<uint64_t> expected_sequences;
    
    for (int i = 0; i < total_events; i++) {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_PUT;
        event.key = "item_" + std::to_string(i);
        event.value = "{\"index\":" + std::to_string(i) + "}";
        
        auto recorded = changefeed_->recordEvent(event);
        expected_sequences.push_back(recorded.sequence);
    }
    
    // Read events in pages of 100
    std::vector<uint64_t> retrieved_sequences;
    uint64_t last_sequence = 0;
    
    while (retrieved_sequences.size() < expected_sequences.size()) {
        Changefeed::ListOptions options;
        options.start_sequence = last_sequence;
        options.limit = 100;
        options.timeout_ms = 0;
        
        auto events = changefeed_->listEvents(options);
        
        if (events.empty()) {
            break;
        }
        
        for (const auto& evt : events) {
            retrieved_sequences.push_back(evt.sequence);
            last_sequence = evt.sequence;
        }
    }
    
    // Verify all sequences match
    ASSERT_EQ(retrieved_sequences.size(), expected_sequences.size());
    
    for (size_t i = 0; i < expected_sequences.size(); i++) {
        EXPECT_EQ(retrieved_sequences[i], expected_sequences[i])
            << "Sequence mismatch at index " << i;
    }
}

// ===== Timestamp Ordering Tests =====

TEST_F(ChangefeedOrderingTest, TimestampsAreMonotonic) {
    std::vector<Changefeed::ChangeEvent> recorded_events;
    
    for (int i = 0; i < 100; i++) {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_PUT;
        event.key = "key_" + std::to_string(i);
        event.value = "{}";
        
        // Don't set timestamp - let changefeed set it
        recorded_events.push_back(changefeed_->recordEvent(event));
        
        // Small delay to ensure time progresses
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Verify timestamps are non-decreasing
    for (size_t i = 1; i < recorded_events.size(); i++) {
        EXPECT_GE(recorded_events[i].timestamp_ms, recorded_events[i-1].timestamp_ms)
            << "Timestamp not monotonic at index " << i;
    }
}

// ===== Mixed Event Type Ordering =====

TEST_F(ChangefeedOrderingTest, MixedEventTypesPreserveOrder) {
    std::vector<Changefeed::ChangeEventType> event_types = {
        Changefeed::ChangeEventType::EVENT_PUT,
        Changefeed::ChangeEventType::EVENT_DELETE,
        Changefeed::ChangeEventType::EVENT_PUT,
        Changefeed::ChangeEventType::EVENT_TRANSACTION_COMMIT,
        Changefeed::ChangeEventType::EVENT_PUT,
        Changefeed::ChangeEventType::EVENT_TRANSACTION_ROLLBACK,
        Changefeed::ChangeEventType::EVENT_DELETE
    };
    
    std::vector<Changefeed::ChangeEvent> recorded_events;
    
    for (size_t i = 0; i < event_types.size(); i++) {
        Changefeed::ChangeEvent event;
        event.type = event_types[i];
        event.key = "resource_1";
        
        if (event.type == Changefeed::ChangeEventType::EVENT_PUT ||
            event.type == Changefeed::ChangeEventType::EVENT_TRANSACTION_COMMIT) {
            event.value = "{\"version\":" + std::to_string(i) + "}";
        }
        
        recorded_events.push_back(changefeed_->recordEvent(event));
    }
    
    // Query and verify order
    Changefeed::ListOptions options;
    options.start_sequence = 0;
    options.limit = 100;
    
    auto events = changefeed_->listEvents(options);
    
    ASSERT_EQ(events.size(), event_types.size());
    
    for (size_t i = 0; i < events.size(); i++) {
        EXPECT_EQ(events[i].type, event_types[i])
            << "Event type mismatch at index " << i;
    }
}

// ===== Gap Detection Test =====

TEST_F(ChangefeedOrderingTest, NoSequenceGaps) {
    // Record events
    const int num_events = 100;
    std::vector<uint64_t> sequences;
    
    for (int i = 0; i < num_events; i++) {
        Changefeed::ChangeEvent event;
        event.type = Changefeed::ChangeEventType::EVENT_PUT;
        event.key = "item_" + std::to_string(i);
        event.value = "{}";
        
        auto recorded = changefeed_->recordEvent(event);
        sequences.push_back(recorded.sequence);
    }
    
    // Verify no gaps in sequence numbers
    std::sort(sequences.begin(), sequences.end());
    
    for (size_t i = 1; i < sequences.size(); i++) {
        EXPECT_EQ(sequences[i], sequences[i-1] + 1)
            << "Gap in sequence at index " << i
            << " (expected " << (sequences[i-1] + 1) 
            << ", got " << sequences[i] << ")";
    }
}
