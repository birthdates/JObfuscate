package com.testapp.service;

public interface PaymentGateway extends Auditable {
    String getProviderName();
    boolean processPayment(double amount, String currency);
    double calculateFee(double amount);

    @Override
    default String getAuditTag() {
        return "PAYMENT_GATEWAY:" + getProviderName();
    }
}
