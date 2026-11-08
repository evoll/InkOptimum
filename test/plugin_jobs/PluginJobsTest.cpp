#include <PluginJobs.h>
#include <gtest/gtest.h>

using namespace pluginjobs;

TEST(PluginJobs, EnqueueClaimComplete) {
  Queue q;
  const uint32_t id = q.enqueue("p", "run", "{\"a\":1}", 0);
  ASSERT_NE(id, 0u);
  EXPECT_EQ(q.status(id).state, State::Pending);
  EXPECT_EQ(q.claim("other", 0).id, 0u);
  auto c = q.claim("p", 10);
  ASSERT_EQ(c.id, id);
  EXPECT_EQ(c.action, "run");
  EXPECT_EQ(c.args, "{\"a\":1}");
  EXPECT_EQ(q.status(id).state, State::Running);
  EXPECT_FALSE(q.complete(id, c.claim + 1, true, "{}"));
  EXPECT_TRUE(q.complete(id, c.claim, true, "{\"x\":2}"));
  EXPECT_EQ(q.status(id).state, State::Done);
  EXPECT_EQ(q.status(id).result, "{\"x\":2}");
}

TEST(PluginJobs, FullPoolRejectsUntilFinished) {
  Queue q;
  uint32_t ids[SLOTS];
  for (int i = 0; i < SLOTS; i++) ids[i] = q.enqueue("p", "a", "", 0);
  EXPECT_EQ(q.enqueue("p", "a", "", 0), 0u);
  auto c = q.claim("p", 0);
  EXPECT_TRUE(q.complete(c.id, c.claim, false, "{}"));
  const uint32_t fresh = q.enqueue("p", "a", "", 0);
  EXPECT_NE(fresh, 0u);
  EXPECT_EQ(q.status(c.id).state, State::Unknown);  // recycled
}

TEST(PluginJobs, ExpiredLeaseIsReclaimedAndOldClaimIsStale) {
  Queue q;
  const uint32_t id = q.enqueue("p", "a", "", 0);
  auto first = q.claim("p", 0);
  EXPECT_EQ(q.claim("p", 1000).id, 0u);
  auto second = q.claim("p", LEASE_MS + 1);
  ASSERT_EQ(second.id, id);
  EXPECT_NE(second.claim, first.claim);
  EXPECT_FALSE(q.complete(id, first.claim, true, "{}"));
  EXPECT_TRUE(q.complete(id, second.claim, true, "{}"));
}

TEST(PluginJobs, RejectsOversizedInput) {
  Queue q;
  EXPECT_EQ(q.enqueue("", "a", "", 0), 0u);
  EXPECT_EQ(q.enqueue("p", std::string(24, 'x'), "", 0), 0u);
  EXPECT_EQ(q.enqueue("p", "a", std::string(MAX_JSON + 1, 'x'), 0), 0u);
}
