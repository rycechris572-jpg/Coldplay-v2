#include <Geode/Geode.hpp>
#include "Bridge.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Windows.h>
#include <cstdio>
#include <algorithm>

using namespace geode::prelude;

// ============================================================================
// COLDPLAY - PREDICTION ENGINE SETTINGS
// ============================================================================

// Bypass expensive prediction checks after rapid consecutive Wave inputs.
constexpr int SPAM_INTERVAL_TICKS = 8;
constexpr int SPAM_STREAK_REQUIRED = 1;

// ============================================================================

class TrajectoryBufferSim {
private:
    PlayerObject* m_simPlayer1 = nullptr;
    PlayerObject* m_simPlayer2 = nullptr;

    bool m_simulating = false;
    bool m_firingBuffered = false;
    bool m_simulationDead = false;

    // Physical input state versus game input state.
    bool m_physicalHolding = false;
    bool m_gameHolding = false;

    // Preset state.
    int m_presetIndex = 0;
    bool m_enabled = true;
    int m_maxTicksNormal = 2;
    int m_maxTicksWave = 2;

    // Input buffer state.
    int m_waitTicks = -1;
    bool m_bufferedPush = true;
    bool m_queuedOpposite = false;
    float m_frameDt = 0.25f;

    // Wave spam guard.
    int m_ticksSinceLastInput = 999;
    int m_rapidInputStreak = 0;

    PlayerObject* createTrajectoryPlayer() {
        PlayLayer* pl = PlayLayer::get();
        if (!pl) return nullptr;

        PlayerObject* player = PlayerObject::create(1, 1, pl, pl, true);
        if (!player) return nullptr;

        player->retain();
        player->setPosition({0, 105});
        player->setVisible(false);
        pl->m_objectLayer->addChild(player);

        return player;
    }

    void resetCollisionLog(PlayerObject* self) {
        self->m_collisionLogTop->removeAllObjects();
        self->m_collisionLogBottom->removeAllObjects();
        self->m_collisionLogLeft->removeAllObjects();
        self->m_collisionLogRight->removeAllObjects();
    }

    void syncSimPlayer(PlayerObject* sim, PlayerObject* real) {
        sim->setVisible(false);
        sim->copyAttributes(real);
        sim->setScale(real->getScale());
        sim->setContentSize(real->getContentSize());

        sim->m_width = real->m_width;
        sim->m_height = real->m_height;
        sim->m_gravityMod = real->m_gravityMod;
        sim->m_isOnGround = real->m_isOnGround;
        sim->m_isUpsideDown = real->m_isUpsideDown;
        sim->m_vehicleSize = real->m_vehicleSize;
        sim->m_playerSpeed = real->m_playerSpeed;
    }

    float getValidDt() const {
        if (m_frameDt > 0.001f && m_frameDt < 2.0f) {
            return m_frameDt;
        }

        return 0.25f;
    }

public:
    void init() {
        PlayLayer* pl = PlayLayer::get();
        if (!pl) return;

        m_simPlayer1 = createTrajectoryPlayer();
        m_simPlayer2 = createTrajectoryPlayer();

        m_waitTicks = -1;
        m_queuedOpposite = false;

        m_physicalHolding = false;
        m_gameHolding = false;

        m_simulating = false;
        m_firingBuffered = false;

        m_ticksSinceLastInput = 999;
        m_rapidInputStreak = 0;
    }

    void quit() {
        if (m_simPlayer1) {
            m_simPlayer1->release();
            m_simPlayer1 = nullptr;
        }

        if (m_simPlayer2) {
            m_simPlayer2->release();
            m_simPlayer2 = nullptr;
        }

        m_simulating = false;
        m_firingBuffered = false;

        m_waitTicks = -1;
        m_queuedOpposite = false;

        m_rapidInputStreak = 0;
    }

    // F6 cycles through the available presets and OFF.
    // Maximum prediction is 60 ticks.
    void cyclePreset() {
        m_presetIndex = (m_presetIndex + 1) % 10;

        m_waitTicks = -1;
        m_queuedOpposite = false;

        constexpr int normalTicks[] = {
            2, 4, 8, 16, 24, 32, 48, 54, 60, 0
        };

        constexpr int waveTicks[] = {
            2, 4, 8, 8, 24, 32, 48, 54, 60, 0
        };

        m_enabled = m_presetIndex != 9;

        m_maxTicksNormal = normalTicks[m_presetIndex];
        m_maxTicksWave = waveTicks[m_presetIndex];

        if (m_enabled) {
            printf(
                "[COLDPLAY] Stage %d: %d Normal / %d Wave\n",
                m_presetIndex + 1,
                m_maxTicksNormal,
                m_maxTicksWave
            );
        } else {
            printf("[COLDPLAY] Buffer OFF\n");
        }
    }

    int normalTicks() const {
        return m_maxTicksNormal;
    }

    int waveTicks() const {
        return m_maxTicksWave;
    }

    bool enabled() const {
        return m_enabled;
    }

    bool isSimulating() const {
        return m_simulating;
    }

    bool isFiringBuffered() const {
        return m_firingBuffered;
    }

    bool checkSimulationDeath(PlayerObject* player) {
        if (m_simulating &&
            (player == m_simPlayer1 || player == m_simPlayer2)) {
            m_simulationDead = true;
            return true;
        }

        // Real player died. Clear pending input.
        m_waitTicks = -1;
        m_queuedOpposite = false;
        m_gameHolding = false;
        m_rapidInputStreak = 0;

        return false;
    }

    void setFrameDelta(float dt) {
        PlayLayer* pl = PlayLayer::get();

        if (pl && pl->m_gameState.m_timeWarp > 0.f) {
            m_frameDt = dt / pl->m_gameState.m_timeWarp;
        } else {
            m_frameDt = dt;
        }
    }

    int getSurvivalTicks(
        PlayLayer* pl,
        PlayerObject* realP1,
        PlayerObject* realP2,
        bool isDual,
        int delayFrames,
        bool actionPush,
        int maxTicks
    ) {
        if (!pl || !m_simPlayer1 || !realP1) {
            return maxTicks;
        }

        m_simulating = true;
        m_simulationDead = false;

        syncSimPlayer(m_simPlayer1, realP1);

        if (isDual && m_simPlayer2 && realP2) {
            syncSimPlayer(m_simPlayer2, realP2);
        }

        // Maintain the pre-input button state.
        if (delayFrames > 0) {
            if (!actionPush) {
                m_simPlayer1->pushButton(PlayerButton::Jump);

                if (isDual && m_simPlayer2) {
                    m_simPlayer2->pushButton(PlayerButton::Jump);
                }
            } else {
                m_simPlayer1->releaseButton(PlayerButton::Jump);

                if (isDual && m_simPlayer2) {
                    m_simPlayer2->releaseButton(PlayerButton::Jump);
                }
            }
        }

        float dt = getValidDt();

        int survived = 0;

        bool isContinuousMode =
            realP1->m_isDart ||
            realP1->m_isBird ||
            realP1->m_isSwing ||
            (
                isDual &&
                realP2 &&
                (
                    realP2->m_isDart ||
                    realP2->m_isBird ||
                    realP2->m_isSwing
                )
            );

        int airborneTicks = 0;
        int safeLandedTicks = 0;

        for (int i = 0; i < delayFrames + maxTicks; i++) {
            if (i == delayFrames) {
                if (actionPush) {
                    m_simPlayer1->pushButton(PlayerButton::Jump);

                    if (isDual && m_simPlayer2) {
                        m_simPlayer2->pushButton(PlayerButton::Jump);
                    }
                } else {
                    m_simPlayer1->releaseButton(PlayerButton::Jump);

                    if (isDual && m_simPlayer2) {
                        m_simPlayer2->releaseButton(PlayerButton::Jump);
                    }
                }
            }

            m_simPlayer1->update(dt);
            m_simPlayer1->updateRotation(dt);

            if (isDual && m_simPlayer2) {
                m_simPlayer2->update(dt);
                m_simPlayer2->updateRotation(dt);
            }

            if (m_simulationDead) break;

            resetCollisionLog(m_simPlayer1);
            pl->checkCollisions(m_simPlayer1, dt, false);

            if (m_simulationDead) break;

            if (isDual && m_simPlayer2 && realP2) {
                resetCollisionLog(m_simPlayer2);
                pl->checkCollisions(m_simPlayer2, dt, false);

                if (m_simulationDead) break;
            }

            if (i >= delayFrames) {
                survived++;

                if (!isContinuousMode) {
                    bool p1Grounded = m_simPlayer1->m_isOnGround;

                    bool p2Grounded =
                        (!isDual || !m_simPlayer2)
                        ? true
                        : m_simPlayer2->m_isOnGround;

                    if (!p1Grounded || !p2Grounded) {
                        airborneTicks++;
                        safeLandedTicks = 0;
                    } else if (airborneTicks >= 15) {
                        safeLandedTicks++;

                        if (safeLandedTicks >= 10) {
                            survived = maxTicks;
                            break;
                        }
                    }
                }
            }
        }

        m_simulating = false;

        return survived;
    }

    bool interceptInput(
        GJBaseGameLayer* layer,
        bool push,
        bool isPlayer1
    ) {
        PlayLayer* pl = PlayLayer::get();
        if (!pl) return true;

        bool isDual =
            pl->m_gameState.m_isDualMode &&
            pl->m_player2 != nullptr;

        bool isTwoPlayerControl =
            pl->m_level &&
            pl->m_level->m_twoPlayerMode;

        // Keep Player 2 synced with Player 1 in 1-player Dual mode.
        if (!isPlayer1) {
            if (
                isDual &&
                !isTwoPlayerControl &&
                (m_waitTicks >= 0 || m_waitTicks == -2)
            ) {
                return false;
            }

            return true;
        }

        // Record physical input state.
        m_physicalHolding = push;

        if (!m_enabled) {
            m_gameHolding = push;
            return true;
        }

        PlayerObject* p1 = layer->m_player1;
        PlayerObject* p2 = isDual ? pl->m_player2 : nullptr;

        if (!p1 || p1->m_isDead) {
            m_waitTicks = -1;
            m_queuedOpposite = false;
            m_gameHolding = push;

            return true;
        }

        // Never modify Ship mode.
        if (
            p1->m_isShip ||
            (isDual && p2 && p2->m_isShip)
        ) {
            m_waitTicks = -1;
            m_queuedOpposite = false;
            m_gameHolding = push;

            return true;
        }

        // Handle an input already waiting in the buffer.
        if (m_waitTicks >= 0) {
            if (push != m_bufferedPush) {
                m_queuedOpposite = true;
            } else {
                m_queuedOpposite = false;
            }

            m_ticksSinceLastInput = 0;

            return false;
        }

        // Handle a queued opposite action.
        if (m_waitTicks == -2) {
            m_waitTicks = -1;
            m_queuedOpposite = false;

            if (m_gameHolding != !m_bufferedPush) {
                m_SimFireButton(pl, !m_bufferedPush);
            }
        }

        bool isWave =
            p1->m_isDart ||
            (isDual && p2 && p2->m_isDart);

        // Wave spam guard.
        if (m_ticksSinceLastInput < SPAM_INTERVAL_TICKS) {
            m_rapidInputStreak++;
        } else {
            m_rapidInputStreak = 0;
        }

        m_ticksSinceLastInput = 0;

        // Skip expensive trajectory prediction during rapid Wave spam.
        if (
            isWave &&
            m_rapidInputStreak >= SPAM_STREAK_REQUIRED
        ) {
            m_gameHolding = push;
            return true;
        }

        // Only Wave and Robot change trajectory on release.
        bool releaseAffectsPhysics =
            p1->m_isDart ||
            p1->m_isRobot ||
            (
                isDual &&
                p2 &&
                (p2->m_isDart || p2->m_isRobot)
            );

        if (!push && !releaseAffectsPhysics) {
            m_gameHolding = false;
            return true;
        }

        m_queuedOpposite = false;

        const char* act = push ? "PRESS" : "RELEASE";

        // Prediction horizon.
        int isMiniWave =
            (p1->m_isDart && p1->m_vehicleSize < 1.0f) ||
            (
                isDual &&
                p2 &&
                p2->m_isDart &&
                p2->m_vehicleSize < 1.0f
            );

        int targetHorizon =
            isMiniWave ? 50 :
            (isWave ? 65 : 160);

        int maxDelayFrames =
            isWave ? m_maxTicksWave : m_maxTicksNormal;

        // Check immediate input.
        int ticksFrame0 = getSurvivalTicks(
            pl,
            p1,
            p2,
            isDual,
            0,
            push,
            targetHorizon
        );

        if (ticksFrame0 >= targetHorizon) {
            m_gameHolding = push;
            return true;
        }

        // Search for the earliest safe delay.
        int bestDelay = 0;
        int bestTicks = ticksFrame0;

        for (
            int delay = 1;
            delay <= maxDelayFrames;
            delay++
        ) {
            int ticks = getSurvivalTicks(
                pl,
                p1,
                p2,
                isDual,
                delay,
                push,
                targetHorizon
            );

            if (isWave) {
                int minClearedGain = (delay * 2) + 10;

                if (
                    ticks >= targetHorizon &&
                    (ticks - ticksFrame0) >= (delay * 2) + 6
                ) {
                    bestDelay = delay;
                    bestTicks = ticks;
                    break;
                }

                if (
                    ticks >= ticksFrame0 + minClearedGain &&
                    ticks > bestTicks + 2
                ) {
                    bestDelay = delay;
                    bestTicks = ticks;

                    if (ticks >= targetHorizon) {
                        break;
                    }
                }
            } else {
                if (ticks >= targetHorizon) {
                    bestDelay = delay;
                    bestTicks = ticks;
                    break;
                }

                if (ticks > bestTicks + 12) {
                    bestDelay = delay;
                    bestTicks = ticks;
                }
            }
        }

        if (bestDelay > 0) {
            printf(
                "[COLDPLAY] Saved +%d ticks (%.2f frames @ 60fps) %s "
                "(Frame0: %d -> Delayed: %d) [Limit: %d]\n",
                bestDelay,
                bestDelay / 4.0f,
                act,
                ticksFrame0,
                bestTicks,
                maxDelayFrames
            );

            m_bufferedPush = push;
            m_waitTicks = bestDelay;

            return false;
        }

        m_gameHolding = push;

        return true;
    }

    void onPlayerUpdate(PlayerObject* player) {
        PlayLayer* pl = PlayLayer::get();

        if (!pl || player != pl->m_player1) {
            return;
        }

        // F6 can always be used to cycle presets.
        if (GetAsyncKeyState(VK_F6) & 1) {
            cyclePreset();
        }

        if (!m_enabled) {
            return;
        }

        if (m_ticksSinceLastInput < 999) {
            m_ticksSinceLastInput++;
        }

        // Ship mode bypass.
        if (
            player->m_isShip ||
            (
                pl->m_gameState.m_isDualMode &&
                pl->m_player2 &&
                pl->m_player2->m_isShip
            )
        ) {
            m_waitTicks = -1;
            m_queuedOpposite = false;

            if (m_gameHolding != m_physicalHolding) {
                m_SimFireButton(pl, m_physicalHolding);
            }

            return;
        }

        if (m_waitTicks == 0) {
            m_SimFireButton(pl, m_bufferedPush);

            if (m_queuedOpposite) {
                m_waitTicks = -2;
                m_queuedOpposite = false;
            } else {
                m_waitTicks = -1;
            }
        } else if (m_waitTicks > 0) {
            m_waitTicks--;
        } else if (m_waitTicks == -2) {
            m_SimFireButton(pl, !m_bufferedPush);

            m_queuedOpposite = false;
            m_waitTicks = -1;
        } else if (m_waitTicks == -1) {
            // Keep game input synchronized with physical input.
            if (m_gameHolding != m_physicalHolding) {
                m_SimFireButton(pl, m_physicalHolding);
            }
        }
    }

    void m_SimFireButton(PlayLayer* pl, bool push) {
        m_firingBuffered = true;
        m_gameHolding = push;

        pl->handleButton(push, 1, true);

        if (
            pl->m_gameState.m_isDualMode &&
            (!pl->m_level || !pl->m_level->m_twoPlayerMode)
        ) {
            pl->handleButton(push, 1, false);
        }

        m_firingBuffered = false;
    }
};

// ============================================================================
// ENGINE INSTANCE
// ============================================================================

static TrajectoryBufferSim s_sim;

// ============================================================================
// COLDPLAY STARTUP
// ============================================================================

static ColdplayBridge s_bridge;

$on_mod(Loaded) {
    AllocConsole();

    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    SetConsoleTitleA("COLDPLAY - Prediction Engine");

    printf("============================================================\n");
    printf(" COLDPLAY PREDICTION ENGINE\n");
    printf(" Maximum prediction: 60 ticks\n");
    printf(" In-game tick display: Disabled\n");
    printf(" 120-tick mode: Removed\n");
    printf(" Wave spam optimization: Enabled\n");
    printf("============================================================\n");

    s_bridge.start();
}

$on_mod(Unloaded) {
    s_bridge.stop();
}
  
// ============================================================================
// PLAY LAYER
// ============================================================================

class $modify(BufPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "PlayLayer::destroyPlayer",
            -1000000
        );

        (void)self.setHookPriority(
            "PlayLayer::playEndAnimationToPos",
            -1000000
        );

        (void)self.setHookPriority(
            "PlayLayer::flipGravity",
            -1000000
        );
    }

    bool init(
        GJGameLevel* level,
        bool useReplay,
        bool dontCreateObjects
    ) {
        if (!PlayLayer::init(
            level,
            useReplay,
            dontCreateObjects
        )) {
            return false;
        }

        s_sim.init();

        return true;
    }

    void destroyPlayer(
        PlayerObject* player,
        GameObject* gameObject
    ) {
        if (s_sim.checkSimulationDeath(player)) {
            return;
        }

        PlayLayer::destroyPlayer(player, gameObject);
    }

    void playEndAnimationToPos(cocos2d::CCPoint p0) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayLayer::playEndAnimationToPos(p0);
    }

    void flipGravity(
        PlayerObject* p0,
        bool p1,
        bool p2
    ) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayLayer::flipGravity(p0, p1, p2);
    }

    void onQuit() {
        PlayLayer::onQuit();
        s_sim.quit();
    }
};

// ============================================================================
// EDITOR
// ============================================================================

class $modify(BufEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool unk) {
        bool res = LevelEditorLayer::init(level, unk);

        s_sim.quit();

        return res;
    }
};

// ============================================================================
// HARD STREAK
// ============================================================================

class $modify(BufHardStreak, HardStreak) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "HardStreak::addPoint",
            -1000000
        );
    }

    void addPoint(cocos2d::CCPoint p0) {
        if (s_sim.isSimulating()) {
            return;
        }

        HardStreak::addPoint(p0);
    }
};

// ============================================================================
// EFFECT GAME OBJECT
// ============================================================================

class $modify(BufEffectGameObject, EffectGameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "EffectGameObject::triggerObject",
            -1000000
        );
    }

    void triggerObject(
        GJBaseGameLayer* p0,
        int p1,
        const gd::vector<int>* p2
    ) {
        if (s_sim.isSimulating()) {
            return;
        }

        EffectGameObject::triggerObject(p0, p1, p2);
    }
};

// ============================================================================
// GAME OBJECT
// ============================================================================

class $modify(BufGameObject, GameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "GameObject::playShineEffect",
            -1000000
        );
    }

    void playShineEffect() {
        if (s_sim.isSimulating()) {
            return;
        }

        GameObject::playShineEffect();
    }
};

// ============================================================================
// PLAYER OBJECT
// ============================================================================

class $modify(BufPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "PlayerObject::playSpiderDashEffect",
            -1000000
        );

        (void)self.setHookPriority(
            "PlayerObject::incrementJumps",
            -1000000
        );

        (void)self.setHookPriority(
            "PlayerObject::ringJump",
            -1000000
        );
    }

    void playSpiderDashEffect(
        cocos2d::CCPoint from,
        cocos2d::CCPoint to
    ) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayerObject::incrementJumps();
    }

    void update(float dt) {
        if (!s_sim.isSimulating()) {
            s_sim.onPlayerUpdate(this);
        }

        PlayerObject::update(dt);

        if (PlayLayer::get() && !s_sim.isSimulating()) {
            s_sim.setFrameDelta(dt);
        }
    }

    void ringJump(RingObject* p0, bool p1) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayerObject::ringJump(p0, p1);
    }
};

// ============================================================================
// BASE GAME LAYER
// ============================================================================

class $modify(BufBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "GJBaseGameLayer::canBeActivatedByPlayer",
            -1000000
        );

        (void)self.setHookPriority(
            "GJBaseGameLayer::handleButton",
            -1000000
        );

        (void)self.setHookPriority(
            "GJBaseGameLayer::collisionCheckObjects",
            -1000000
        );

        (void)self.setHookPriority(
            "GJBaseGameLayer::playerTouchedRing",
            -1000000
        );
    }

    bool canBeActivatedByPlayer(
        PlayerObject* p0,
        EffectGameObject* p1
    ) {
        if (s_sim.isSimulating()) {
            return false;
        }

        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void handleButton(
        bool down,
        int button,
        bool isPlayer1
    ) {
        if (
            !s_sim.isSimulating() &&
            !s_sim.isFiringBuffered() &&
            button == 1 &&
            PlayLayer::get()
        ) {
            if (!s_sim.interceptInput(this, down, isPlayer1)) {
                return;
            }
        }

        GJBaseGameLayer::handleButton(
            down,
            button,
            isPlayer1
        );
    }

    void collisionCheckObjects(
        PlayerObject* player,
        gd::vector<GameObject*>* vec,
        int objectsCount,
        float dt
    ) {
        if (s_sim.isSimulating()) {
            // Reuse the vector to avoid repeated allocations.
            static gd::vector<GameObject*> extra;

            extra.clear();

            if (
                extra.capacity() <
                static_cast<size_t>(objectsCount)
            ) {
                extra.reserve(objectsCount);
            }

            for (int i = 0; i < objectsCount; i++) {
                GameObject* obj = vec->at(i);

                if (!obj) {
                    continue;
                }

                if (
                    obj->m_objectType == GameObjectType::Solid ||
                    obj->m_objectType == GameObjectType::Hazard ||
                    obj->m_objectType == GameObjectType::AnimatedHazard ||
                    obj->m_objectType == GameObjectType::CollisionObject ||
                    obj->m_objectType == GameObjectType::Slope
                ) {
                    extra.push_back(obj);
                }
            }

            GJBaseGameLayer::collisionCheckObjects(
                player,
                &extra,
                static_cast<int>(extra.size()),
                dt
            );

            return;
        }

        GJBaseGameLayer::collisionCheckObjects(
            player,
            vec,
            objectsCount,
            dt
        );
    }

    void playerTouchedRing(
        PlayerObject* player,
        RingObject* ring
    ) {
        if (s_sim.isSimulating()) {
            return;
        }

        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
};
