package com.testapp.service;

public interface Auditable {
    String getAuditTag();

    // Java 8 default method
    default String generateAuditSummary() {
        return "[AUDIT:" + getAuditTag() + "]@" + currentAuditEpoch();
    }

    // Java 8 static method
    static long currentAuditEpoch() {
        return System.currentTimeMillis();
    }
}
