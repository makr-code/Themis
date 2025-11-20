package com.themisdb.client;

import java.time.Duration;

/**
 * Options for creating a transaction
 */
public class TransactionOptions {
    private IsolationLevel isolationLevel;
    private Duration timeout;

    /**
     * Create default transaction options (READ_COMMITTED isolation)
     */
    public TransactionOptions() {
        this.isolationLevel = IsolationLevel.READ_COMMITTED;
        this.timeout = null;
    }

    /**
     * Create transaction options with specified isolation level
     * 
     * @param isolationLevel Desired isolation level
     */
    public TransactionOptions(IsolationLevel isolationLevel) {
        this.isolationLevel = isolationLevel;
        this.timeout = null;
    }

    /**
     * Create transaction options with isolation level and timeout
     * 
     * @param isolationLevel Desired isolation level
     * @param timeout Transaction timeout
     */
    public TransactionOptions(IsolationLevel isolationLevel, Duration timeout) {
        this.isolationLevel = isolationLevel;
        this.timeout = timeout;
    }

    /**
     * Get the isolation level
     * 
     * @return Isolation level
     */
    public IsolationLevel getIsolationLevel() {
        return isolationLevel;
    }

    /**
     * Set the isolation level
     * 
     * @param isolationLevel Isolation level
     * @return This options object (for chaining)
     */
    public TransactionOptions setIsolationLevel(IsolationLevel isolationLevel) {
        this.isolationLevel = isolationLevel;
        return this;
    }

    /**
     * Get the transaction timeout
     * 
     * @return Timeout duration (null if not set)
     */
    public Duration getTimeout() {
        return timeout;
    }

    /**
     * Set the transaction timeout
     * 
     * @param timeout Timeout duration
     * @return This options object (for chaining)
     */
    public TransactionOptions setTimeout(Duration timeout) {
        this.timeout = timeout;
        return this;
    }
}
