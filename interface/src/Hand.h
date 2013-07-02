//
//  Hand.h
//  interface
//
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#ifndef hifi_Hand_h
#define hifi_Hand_h

#include <glm/glm.hpp>
#include <AvatarData.h>
#include <HandData.h>
#include "Balls.h"
#include "world.h"
#include "InterfaceConfig.h"
#include "SerialInterface.h"
#include <SharedUtil.h>


class Avatar;
class ProgramObject;

class Hand : public HandData {
public:
    Hand(Avatar* owningAvatar);
    
    struct HandBall
    {
        glm::vec3        position;       // the actual dynamic position of the ball at any given time
        glm::quat        rotation;       // the rotation of the ball
        glm::vec3        velocity;       // the velocity of the ball
        float            radius;         // the radius of the ball
        bool             isCollidable;   // whether or not the ball responds to collisions
        float            touchForce;     // a scalar determining the amount that the cursor (or hand) is penetrating the ball
    };

    void init();
    void reset();
    void simulate(float deltaTime, bool isMine);
    void render(bool lookingInMirror);

    void setBallColor      (glm::vec3 ballColor         ) { _ballColor          = ballColor;          }
    void setLeapFingers    (const std::vector<glm::vec3>& fingerPositions);

    // getters
    int              getNumLeapBalls           ()                const { return _numLeapBalls;}
    const glm::vec3& getLeapBallPosition       (int which)       const { return _leapBall[which].position;}

private:
    // disallow copies of the Hand, copy of owning Avatar is disallowed too
    Hand(const Hand&);
    Hand& operator= (const Hand&);

    Avatar*     _owningAvatar;
    float       _renderAlpha;
    bool        _lookingInMirror;
    glm::vec3   _ballColor;
    glm::vec3   _position;
    glm::quat   _orientation;
    int         _numLeapBalls;
    HandBall	_leapBall[ MAX_AVATAR_LEAP_BALLS ];
    
    // private methods
    void renderHandSpheres();
    void calculateGeometry();
};

#endif
