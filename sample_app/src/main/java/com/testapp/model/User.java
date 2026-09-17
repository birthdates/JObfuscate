package com.testapp.model;

import com.testapp.annotation.FeatureAudit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

@FeatureAudit(featureName = "UserModel", priority = 2)
public class User {
    private final int id;
    private final String username;
    private String secretToken;
    private Role role;
    private final List<String> tags = new ArrayList<String>();
    private final Object lock = new Object();

    public User(int id, String username, String secretToken, Role role) {
        this.id = id;
        this.username = username;
        this.secretToken = secretToken;
        this.role = role;
    }

    // Static nested class (Builder pattern)
    public static class Builder {
        private int id;
        private String username;
        private String secretToken;
        private Role role = Role.GUEST;

        public Builder id(int id) { this.id = id; return this; }
        public Builder username(String username) { this.username = username; return this; }
        public Builder secretToken(String secretToken) { this.secretToken = secretToken; return this; }
        public Builder role(Role role) { this.role = role; return this; }
        public User build() { return new User(id, username, secretToken, role); }
    }

    // Member inner class (non-static, accesses enclosing instance)
    public class Session {
        private final String sessionId;
        private final long createdTime;

        public Session(String sessionId) {
            this.sessionId = sessionId;
            this.createdTime = System.currentTimeMillis();
        }

        public String getSessionDetails() {
            return "Session[" + sessionId + "] for " + User.this.username + " (Role: " + User.this.role + ")";
        }

        public User getEnclosingUser() {
            return User.this;
        }

        public long getCreatedTime() {
            return createdTime;
        }
    }

    public Session createSession(String sessionId) {
        return new Session(sessionId);
    }

    // Synchronized method
    public synchronized boolean updateSecretToken(String newToken) {
        if (newToken == null || newToken.isEmpty()) return false;
        this.secretToken = newToken;
        return true;
    }

    // Synchronized block with varargs
    public void addTags(String... newTags) {
        if (newTags != null) {
            synchronized (this.lock) {
                for (String tag : newTags) {
                    if (tag != null && !tag.isEmpty()) {
                        this.tags.add(tag);
                    }
                }
            }
        }
    }

    public List<String> getTags() {
        synchronized (this.lock) {
            return Collections.unmodifiableList(new ArrayList<String>(this.tags));
        }
    }

    public int getId() { return id; }
    public String getUsername() { return username; }
    public String getSecretToken() { return secretToken; }
    public Role getRole() { return role; }
    public void setRole(Role role) { this.role = role; }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        User other = (User) o;
        return id == other.id && Objects.equals(username, other.username);
    }

    @Override
    public int hashCode() {
        return Objects.hash(id, username);
    }

    @Override
    public String toString() {
        return "User{id=" + id + ", username='" + username + "', role=" + role + "}";
    }
}
