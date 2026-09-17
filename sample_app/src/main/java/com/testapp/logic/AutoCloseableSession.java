package com.testapp.logic;

public class AutoCloseableSession implements AutoCloseable {
    private final String sessionId;
    private boolean closed = false;

    public AutoCloseableSession(String sessionId) {
        if (sessionId == null || sessionId.isEmpty()) {
            throw new IllegalArgumentException("Session ID cannot be empty or null");
        }
        this.sessionId = sessionId;
    }

    public String executeOperation(int opCode) {
        if (closed) {
            throw new IllegalStateException("Session is closed");
        }
        if (opCode < 0) {
            throw new IllegalArgumentException("Negative opCode not supported: " + opCode);
        }
        return "OP_" + opCode + "_EXECUTED_FOR_" + sessionId;
    }

    public boolean isClosed() {
        return closed;
    }

    @Override
    public void close() {
        this.closed = true;
    }

    // Try-with-resources, Multi-catch, and Finally showcase
    public static String runProtectedTransaction(String id, int opCode) {
        final StringBuilder log = new StringBuilder();
        try (AutoCloseableSession session = new AutoCloseableSession(id)) {
            log.append("[START]");
            String res = session.executeOperation(opCode);
            log.append("[").append(res).append("]");
        } catch (IllegalArgumentException | IllegalStateException ex) {
            log.append("[CAUGHT:").append(ex.getClass().getSimpleName()).append("]");
        } finally {
            log.append("[FINALLY]");
        }
        return log.toString();
    }
}
