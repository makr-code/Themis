package com.themisdb.client;

/**
 * Isolation levels for transactions
 */
public enum IsolationLevel {
    /**
     * Read Committed - transactions see only committed data
     */
    READ_COMMITTED,
    
    /**
     * Snapshot - transactions work with a consistent snapshot of the database
     */
    SNAPSHOT;

    @Override
    public String toString() {
        return name();
    }
}
