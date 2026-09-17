package com.testapp.service;

public class StripePayment implements PaymentGateway {
    private static final String PROVIDER = "Stripe v2025 API";
    private final String apiKey;

    public StripePayment(String apiKey) {
        this.apiKey = apiKey;
    }

    @Override
    public String getProviderName() {
        return PROVIDER;
    }

    @Override
    public boolean processPayment(double amount, String currency) {
        if (amount <= 0) return false;
        // Mock transaction logic
        return apiKey != null && !apiKey.isEmpty() && currency.equalsIgnoreCase("USD");
    }

    @Override
    public double calculateFee(double amount) {
        // 2.9% + 30 cents standard fee
        return Math.round((amount * 0.029 + 0.30) * 100.0) / 100.0;
    }

    private String internalSign(String payload) {
        return "sig_" + payload.hashCode();
    }
}
