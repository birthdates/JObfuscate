package com.testapp.model;

import com.testapp.annotation.FeatureAudit;

@FeatureAudit(featureName = "RoleModel", priority = 3)
public enum Role {
    ADMIN,
    USER,
    GUEST;

    public boolean canAccessAdminPanel() {
        return this == ADMIN;
    }

    public static String getPermissions(Role role) {
        // Enum switch
        switch (role) {
            case ADMIN:
                return "ALL_PERMISSIONS";
            case USER:
                return "USER_PERMISSIONS";
            case GUEST:
            default:
                return "READ_ONLY";
        }
    }
}
