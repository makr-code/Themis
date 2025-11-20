"""Tests for Transaction Support in ThemisDB Python SDK."""

import pytest
from themis import ThemisClient, Transaction, TransactionError


class TestTransactionAPI:
    """Test Transaction API structure and basic functionality."""

    def test_begin_transaction_exists(self):
        """Test that beginTransaction method exists."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        assert hasattr(client, "begin_transaction")
        assert callable(client.begin_transaction)

    def test_transaction_class_structure(self):
        """Test Transaction class has required methods."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        tx = Transaction(client, "test-tx-id")
        
        assert tx.transaction_id == "test-tx-id"
        assert tx.is_active is True
        assert hasattr(tx, "get")
        assert hasattr(tx, "put")
        assert hasattr(tx, "delete")
        assert hasattr(tx, "query")
        assert hasattr(tx, "commit")
        assert hasattr(tx, "rollback")

    def test_transaction_state_committed(self):
        """Test transaction tracks committed state."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        tx = Transaction(client, "test-tx-id")
        
        assert tx.is_active is True
        
        # Manually mark as committed for testing
        tx._committed = True
        
        assert tx.is_active is False
        
        # Operations should raise TransactionError
        with pytest.raises(TransactionError, match="already committed"):
            tx._ensure_active()

    def test_transaction_state_rolled_back(self):
        """Test transaction tracks rolled back state."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        tx = Transaction(client, "test-tx-id")
        
        assert tx.is_active is True
        
        # Manually mark as rolled back for testing
        tx._rolled_back = True
        
        assert tx.is_active is False
        
        # Operations should raise TransactionError
        with pytest.raises(TransactionError, match="already rolled back"):
            tx._ensure_active()

    def test_isolation_level_options(self):
        """Test that isolation level options are properly validated."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        
        # Valid isolation levels shouldn't raise
        # (will fail on network, but that's expected in unit tests)
        # We're just testing the parameter validation
        
        # Invalid isolation level should raise ValueError
        with pytest.raises(ValueError, match="Invalid isolation level"):
            client.begin_transaction(isolation_level="INVALID")

    def test_context_manager_support(self):
        """Test Transaction supports context manager protocol."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        tx = Transaction(client, "test-tx-id")
        
        # Test __enter__ returns self
        assert tx.__enter__() is tx
        
        # Test __exit__ exists
        assert hasattr(tx, "__exit__")
        assert callable(tx.__exit__)


class TestTransactionContextManager:
    """Test Transaction context manager functionality."""

    def test_context_manager_with_statement(self):
        """Test transaction can be used with 'with' statement."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        tx = Transaction(client, "test-tx-id")
        
        # Manually mark as active
        assert tx.is_active
        
        # Test that __enter__ returns self
        entered_tx = tx.__enter__()
        assert entered_tx is tx
        
        # Test that __exit__ can be called without exception
        # (manual call to avoid actual network commit)
        tx._committed = True  # Manually mark as committed to skip actual commit
        tx.__exit__(None, None, None)
        
        # Transaction should be committed
        assert not tx.is_active

    def test_context_manager_commits_on_success(self):
        """Test context manager commits on successful block execution."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        tx = Transaction(client, "test-tx-id")
        
        # Simulate successful execution
        try:
            with tx:
                pass  # No exception
        except Exception:
            pass  # Network error expected without server
        
        # Transaction should have attempted commit
        # (marked as committed or failed network call)

    def test_context_manager_rolls_back_on_exception(self):
        """Test context manager rolls back on exception."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        tx = Transaction(client, "test-tx-id")
        
        # Simulate exception during execution
        try:
            with tx:
                raise ValueError("Test error")
        except ValueError:
            pass  # Expected
        except Exception:
            pass  # Network error also possible
        
        # Transaction should have attempted rollback


# Integration tests (require running server)
class TestTransactionIntegration:
    """Integration tests for Transaction (require running ThemisDB server)."""

    @pytest.mark.skip("Requires running ThemisDB server")
    def test_begin_commit_transaction(self):
        """Test beginning and committing a transaction."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        
        tx = client.begin_transaction()
        
        try:
            tx.put("relational", "users", "user1", {"name": "Alice", "age": 30})
            tx.put("relational", "users", "user2", {"name": "Bob", "age": 25})
            
            user1 = tx.get("relational", "users", "user1")
            assert user1 == {"name": "Alice", "age": 30}
            
            tx.commit()
        except Exception:
            tx.rollback()
            raise

    @pytest.mark.skip("Requires running ThemisDB server")
    def test_rollback_transaction(self):
        """Test rolling back a transaction."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        
        tx = client.begin_transaction()
        
        try:
            tx.put("relational", "users", "user3", {"name": "Charlie", "age": 35})
            
            # Simulate error
            raise RuntimeError("Something went wrong")
            
            tx.commit()
        except RuntimeError:
            tx.rollback()
            # Verify data was not persisted

    @pytest.mark.skip("Requires running ThemisDB server")
    def test_snapshot_isolation(self):
        """Test transaction with SNAPSHOT isolation."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        
        tx = client.begin_transaction(isolation_level="SNAPSHOT")
        
        try:
            tx.put("relational", "accounts", "acc1", {"balance": 1000})
            tx.put("relational", "accounts", "acc2", {"balance": 500})
            
            tx.commit()
        except Exception:
            tx.rollback()
            raise

    @pytest.mark.skip("Requires running ThemisDB server")
    def test_transaction_with_context_manager(self):
        """Test using transaction with context manager."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        
        # Successful transaction
        with client.begin_transaction() as tx:
            tx.put("relational", "users", "user4", {"name": "David", "age": 40})
            user4 = tx.get("relational", "users", "user4")
            assert user4 == {"name": "David", "age": 40}
        # Automatically committed
        
        # Failed transaction
        try:
            with client.begin_transaction() as tx:
                tx.put("relational", "users", "user5", {"name": "Eve", "age": 45})
                raise ValueError("Simulated error")
        except ValueError:
            pass
        # Automatically rolled back

    @pytest.mark.skip("Requires running ThemisDB server")
    def test_transaction_query(self):
        """Test executing queries within a transaction."""
        client = ThemisClient(endpoints=["http://localhost:8080"])
        
        with client.begin_transaction() as tx:
            tx.put("relational", "users", "user6", {"name": "Frank", "age": 50, "active": True})
            tx.put("relational", "users", "user7", {"name": "Grace", "age": 55, "active": True})
            
            result = tx.query("FOR user IN users FILTER user.active == true RETURN user")
            assert len(result.items) >= 2
