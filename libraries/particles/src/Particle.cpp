//
//  Particle.cpp
//  hifi
//
//  Created by Brad Hefta-Gaub on 12/4/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//
//

#include <QtCore/QObject>

#include <Octree.h>
#include <RegisteredMetaTypes.h>
#include <SharedUtil.h> // usecTimestampNow()
#include <VoxelsScriptingInterface.h>

// This is not ideal, but adding script-engine as a linked library, will cause a circular reference
// I'm open to other potential solutions. Could we change cmake to allow libraries to reference each others
// headers, but not link to each other, this is essentially what this construct is doing, but would be
// better to add includes to the include path, but not link
#include "../../script-engine/src/ScriptEngine.h"

#include "ParticlesScriptingInterface.h"
#include "Particle.h"

uint32_t Particle::_nextID = 0;
VoxelEditPacketSender* Particle::_voxelEditSender = NULL;
ParticleEditPacketSender* Particle::_particleEditSender = NULL;

// for locally created particles
std::map<uint32_t,uint32_t> Particle::_tokenIDsToIDs;
uint32_t Particle::_nextCreatorTokenID = 0;

uint32_t Particle::getIDfromCreatorTokenID(uint32_t creatorTokenID) {
    if (_tokenIDsToIDs.find(creatorTokenID) != _tokenIDsToIDs.end()) {
        return _tokenIDsToIDs[creatorTokenID];
    }
    return UNKNOWN_PARTICLE_ID;
}

uint32_t Particle::getNextCreatorTokenID() {
    uint32_t creatorTokenID = _nextCreatorTokenID;
    _nextCreatorTokenID++;
    return creatorTokenID;
}

void Particle::handleAddParticleResponse(unsigned char* packetData , int packetLength) {
    unsigned char* dataAt = packetData;
    int numBytesPacketHeader = numBytesForPacketHeader(packetData);
    dataAt += numBytesPacketHeader;

    uint32_t creatorTokenID;
    memcpy(&creatorTokenID, dataAt, sizeof(creatorTokenID));
    dataAt += sizeof(creatorTokenID);

    uint32_t particleID;
    memcpy(&particleID, dataAt, sizeof(particleID));
    dataAt += sizeof(particleID);

    // add our token to id mapping
    _tokenIDsToIDs[creatorTokenID] = particleID;
}



Particle::Particle(glm::vec3 position, float radius, rgbColor color, glm::vec3 velocity, glm::vec3 gravity,
                    float damping, float lifetime, bool inHand, QString updateScript, uint32_t id) {

    init(position, radius, color, velocity, gravity, damping, lifetime, inHand, updateScript, id);
}

Particle::Particle() {
    rgbColor noColor = { 0, 0, 0 };
    init(glm::vec3(0,0,0), 0, noColor, glm::vec3(0,0,0),
            DEFAULT_GRAVITY, DEFAULT_DAMPING, DEFAULT_LIFETIME, NOT_IN_HAND, DEFAULT_SCRIPT, NEW_PARTICLE);
}

Particle::~Particle() {
}

void Particle::init(glm::vec3 position, float radius, rgbColor color, glm::vec3 velocity, glm::vec3 gravity,
                    float damping, float lifetime, bool inHand, QString updateScript, uint32_t id) {
    if (id == NEW_PARTICLE) {
        _id = _nextID;
        _nextID++;
    } else {
        _id = id;
    }
    uint64_t now = usecTimestampNow();
    _lastEdited = now;
    _lastUpdated = now;
    _created = now; // will get updated as appropriate in setAge()

    _position = position;
    _radius = radius;
    _mass = 1.0f;
    memcpy(_color, color, sizeof(_color));
    _velocity = velocity;
    _damping = damping;
    _lifetime = lifetime;
    _gravity = gravity;
    _script = updateScript;
    _inHand = inHand;
    _shouldDie = false;
}

void Particle::setMass(float value) {
    if (value > 0.0f) {
        _mass = value;
    }
}

bool Particle::appendParticleData(OctreePacketData* packetData) const {

    bool success = packetData->appendValue(getID());

    //printf("Particle::appendParticleData()... getID()=%d\n", getID());

    if (success) {
        success = packetData->appendValue(getAge());
    }
    if (success) {
        success = packetData->appendValue(getLastUpdated());
    }
    if (success) {
        success = packetData->appendValue(getLastEdited());
    }
    if (success) {
        success = packetData->appendValue(getRadius());
    }
    if (success) {
        success = packetData->appendPosition(getPosition());
    }
    if (success) {
        success = packetData->appendColor(getColor());
    }
    if (success) {
        success = packetData->appendValue(getVelocity());
    }
    if (success) {
        success = packetData->appendValue(getGravity());
    }
    if (success) {
        success = packetData->appendValue(getDamping());
    }
    if (success) {
        success = packetData->appendValue(getLifetime());
    }
    if (success) {
        success = packetData->appendValue(getInHand());
    }
    if (success) {
        uint16_t scriptLength = _script.size() + 1; // include NULL
        success = packetData->appendValue(scriptLength);
        if (success) {
            success = packetData->appendRawData((const unsigned char*)qPrintable(_script), scriptLength);
        }
    }
    return success;
}

int Particle::expectedBytes() {
    int expectedBytes = sizeof(uint32_t) // id
                + sizeof(float) // age
                + sizeof(uint64_t) // last updated
                + sizeof(uint64_t) // lasted edited
                + sizeof(float) // radius
                + sizeof(glm::vec3) // position
                + sizeof(rgbColor) // color
                + sizeof(glm::vec3) // velocity
                + sizeof(glm::vec3) // gravity
                + sizeof(float) // damping
                + sizeof(float) // lifetime
                + sizeof(bool); // inhand
                // potentially more...
    return expectedBytes;
}

int Particle::expectedEditMessageBytes() {
    int expectedBytes = sizeof(uint32_t) // id
                + sizeof(uint64_t) // lasted edited
                + sizeof(float) // radius
                + sizeof(glm::vec3) // position
                + sizeof(rgbColor) // color
                + sizeof(glm::vec3) // velocity
                + sizeof(glm::vec3) // gravity
                + sizeof(float) // damping
                + sizeof(float) // lifetime
                + sizeof(bool); // inhand
                // potentially more...
    return expectedBytes;
}

int Particle::readParticleDataFromBuffer(const unsigned char* data, int bytesLeftToRead, ReadBitstreamToTreeParams& args) {
    int bytesRead = 0;
    if (bytesLeftToRead >= expectedBytes()) {
        int clockSkew = args.sourceNode ? args.sourceNode->getClockSkewUsec() : 0;

        const unsigned char* dataAt = data;

        // id
        memcpy(&_id, dataAt, sizeof(_id));
        dataAt += sizeof(_id);
        bytesRead += sizeof(_id);

        // age
        float age;
        memcpy(&age, dataAt, sizeof(age));
        dataAt += sizeof(age);
        bytesRead += sizeof(age);
        setAge(age);

        // _lastUpdated
        memcpy(&_lastUpdated, dataAt, sizeof(_lastUpdated));
        dataAt += sizeof(_lastUpdated);
        bytesRead += sizeof(_lastUpdated);
        _lastUpdated -= clockSkew;

        // _lastEdited
        memcpy(&_lastEdited, dataAt, sizeof(_lastEdited));
        dataAt += sizeof(_lastEdited);
        bytesRead += sizeof(_lastEdited);
        _lastEdited -= clockSkew;

        // radius
        memcpy(&_radius, dataAt, sizeof(_radius));
        dataAt += sizeof(_radius);
        bytesRead += sizeof(_radius);

        // position
        memcpy(&_position, dataAt, sizeof(_position));
        dataAt += sizeof(_position);
        bytesRead += sizeof(_position);

        // color
        memcpy(_color, dataAt, sizeof(_color));
        dataAt += sizeof(_color);
        bytesRead += sizeof(_color);

        // velocity
        memcpy(&_velocity, dataAt, sizeof(_velocity));
        dataAt += sizeof(_velocity);
        bytesRead += sizeof(_velocity);

        // gravity
        memcpy(&_gravity, dataAt, sizeof(_gravity));
        dataAt += sizeof(_gravity);
        bytesRead += sizeof(_gravity);

        // damping
        memcpy(&_damping, dataAt, sizeof(_damping));
        dataAt += sizeof(_damping);
        bytesRead += sizeof(_damping);

        // lifetime
        memcpy(&_lifetime, dataAt, sizeof(_lifetime));
        dataAt += sizeof(_lifetime);
        bytesRead += sizeof(_lifetime);

        // inHand
        memcpy(&_inHand, dataAt, sizeof(_inHand));
        dataAt += sizeof(_inHand);
        bytesRead += sizeof(_inHand);

        // script
        uint16_t scriptLength;
        memcpy(&scriptLength, dataAt, sizeof(scriptLength));
        dataAt += sizeof(scriptLength);
        bytesRead += sizeof(scriptLength);
        QString tempString((const char*)dataAt);
        _script = tempString;
        dataAt += scriptLength;
        bytesRead += scriptLength;

        //printf("Particle::readParticleDataFromBuffer()... "); debugDump();
    }
    return bytesRead;
}


Particle Particle::fromEditPacket(unsigned char* data, int length, int& processedBytes) {
    Particle newParticle; // id and _lastUpdated will get set here...
    unsigned char* dataAt = data;
    processedBytes = 0;

    // the first part of the data is our octcode...
    int octets = numberOfThreeBitSectionsInCode(data);
    int lengthOfOctcode = bytesRequiredForCodeLength(octets);

    // we don't actually do anything with this octcode...
    dataAt += lengthOfOctcode;
    processedBytes += lengthOfOctcode;

    // id
    uint32_t editID;
    memcpy(&editID, dataAt, sizeof(editID));
    dataAt += sizeof(editID);
    processedBytes += sizeof(editID);

    // special case for handling "new" particles
    if (editID == NEW_PARTICLE) {
        // If this is a NEW_PARTICLE, then we assume that there's an additional uint32_t creatorToken, that
        // we want to send back to the creator as an map to the actual id
        uint32_t creatorTokenID;
        memcpy(&creatorTokenID, dataAt, sizeof(creatorTokenID));
        dataAt += sizeof(creatorTokenID);
        processedBytes += sizeof(creatorTokenID);
        newParticle.setCreatorTokenID(creatorTokenID);
        newParticle._newlyCreated = true;

        newParticle.setAge(0); // this guy is new!

    } else {
        newParticle._id = editID;
        newParticle._newlyCreated = false;
    }

    // lastEdited
    memcpy(&newParticle._lastEdited, dataAt, sizeof(newParticle._lastEdited));
    dataAt += sizeof(newParticle._lastEdited);
    processedBytes += sizeof(newParticle._lastEdited);

    // radius
    memcpy(&newParticle._radius, dataAt, sizeof(newParticle._radius));
    dataAt += sizeof(newParticle._radius);
    processedBytes += sizeof(newParticle._radius);

    // position
    memcpy(&newParticle._position, dataAt, sizeof(newParticle._position));
    dataAt += sizeof(newParticle._position);
    processedBytes += sizeof(newParticle._position);

    // color
    memcpy(newParticle._color, dataAt, sizeof(newParticle._color));
    dataAt += sizeof(newParticle._color);
    processedBytes += sizeof(newParticle._color);

    // velocity
    memcpy(&newParticle._velocity, dataAt, sizeof(newParticle._velocity));
    dataAt += sizeof(newParticle._velocity);
    processedBytes += sizeof(newParticle._velocity);

    // gravity
    memcpy(&newParticle._gravity, dataAt, sizeof(newParticle._gravity));
    dataAt += sizeof(newParticle._gravity);
    processedBytes += sizeof(newParticle._gravity);

    // damping
    memcpy(&newParticle._damping, dataAt, sizeof(newParticle._damping));
    dataAt += sizeof(newParticle._damping);
    processedBytes += sizeof(newParticle._damping);

    // lifetime
    memcpy(&newParticle._lifetime, dataAt, sizeof(newParticle._lifetime));
    dataAt += sizeof(newParticle._lifetime);
    processedBytes += sizeof(newParticle._lifetime);

    // inHand
    memcpy(&newParticle._inHand, dataAt, sizeof(newParticle._inHand));
    dataAt += sizeof(newParticle._inHand);
    processedBytes += sizeof(newParticle._inHand);

    // script
    uint16_t scriptLength;
    memcpy(&scriptLength, dataAt, sizeof(scriptLength));
    dataAt += sizeof(scriptLength);
    processedBytes += sizeof(scriptLength);
    QString tempString((const char*)dataAt);
    newParticle._script = tempString;
    dataAt += scriptLength;
    processedBytes += scriptLength;

    const bool wantDebugging = true;
    if (wantDebugging) {
        qDebug("Particle::fromEditPacket()...");
        qDebug("   Particle id in packet:%u", editID);
        //qDebug() << "    position: " << newParticle._position;
        newParticle.debugDump();
    }

    return newParticle;
}

void Particle::debugDump() const {
    printf("Particle id  :%u\n", _id);
    printf(" age:%f\n", getAge());
    printf(" edited ago:%f\n", getEditedAgo());
    printf(" position:%f,%f,%f\n", _position.x, _position.y, _position.z);
    printf(" velocity:%f,%f,%f\n", _velocity.x, _velocity.y, _velocity.z);
    printf(" gravity:%f,%f,%f\n", _gravity.x, _gravity.y, _gravity.z);
    printf(" color:%d,%d,%d\n", _color[0], _color[1], _color[2]);
}

bool Particle::encodeParticleEditMessageDetails(PACKET_TYPE command, int count, const ParticleDetail* details,
        unsigned char* bufferOut, int sizeIn, int& sizeOut) {

    bool success = true; // assume the best
    unsigned char* copyAt = bufferOut;
    sizeOut = 0;

    for (int i = 0; i < count && success; i++) {
        // get the octal code for the particle
        unsigned char* octcode = pointToOctalCode(details[i].position.x, details[i].position.y,
                                                    details[i].position.z, details[i].radius);

        int octets = numberOfThreeBitSectionsInCode(octcode);
        int lengthOfOctcode = bytesRequiredForCodeLength(octets);
        int lenfthOfEditData = lengthOfOctcode + expectedEditMessageBytes();

        // make sure we have room to copy this particle
        if (sizeOut + lenfthOfEditData > sizeIn) {
            success = false;
        } else {
            // add it to our message
            memcpy(copyAt, octcode, lengthOfOctcode);
            copyAt += lengthOfOctcode;
            sizeOut += lengthOfOctcode;

            // Now add our edit content details...

            // id
            memcpy(copyAt, &details[i].id, sizeof(details[i].id));
            copyAt += sizeof(details[i].id);
            sizeOut += sizeof(details[i].id);
            // special case for handling "new" particles
            if (details[i].id == NEW_PARTICLE) {
                // If this is a NEW_PARTICLE, then we assume that there's an additional uint32_t creatorToken, that
                // we want to send back to the creator as an map to the actual id
                memcpy(copyAt, &details[i].creatorTokenID, sizeof(details[i].creatorTokenID));
                copyAt += sizeof(details[i].creatorTokenID);
                sizeOut += sizeof(details[i].creatorTokenID);
            }

            // lastEdited
            memcpy(copyAt, &details[i].lastEdited, sizeof(details[i].lastEdited));
            copyAt += sizeof(details[i].lastEdited);
            sizeOut += sizeof(details[i].lastEdited);

            // radius
            memcpy(copyAt, &details[i].radius, sizeof(details[i].radius));
            copyAt += sizeof(details[i].radius);
            sizeOut += sizeof(details[i].radius);

            // position
            memcpy(copyAt, &details[i].position, sizeof(details[i].position));
            copyAt += sizeof(details[i].position);
            sizeOut += sizeof(details[i].position);

            // color
            memcpy(copyAt, details[i].color, sizeof(details[i].color));
            copyAt += sizeof(details[i].color);
            sizeOut += sizeof(details[i].color);

            // velocity
            memcpy(copyAt, &details[i].velocity, sizeof(details[i].velocity));
            copyAt += sizeof(details[i].velocity);
            sizeOut += sizeof(details[i].velocity);

            // gravity
            memcpy(copyAt, &details[i].gravity, sizeof(details[i].gravity));
            copyAt += sizeof(details[i].gravity);
            sizeOut += sizeof(details[i].gravity);

            // damping
            memcpy(copyAt, &details[i].damping, sizeof(details[i].damping));
            copyAt += sizeof(details[i].damping);
            sizeOut += sizeof(details[i].damping);

            // damping
            memcpy(copyAt, &details[i].lifetime, sizeof(details[i].lifetime));
            copyAt += sizeof(details[i].lifetime);
            sizeOut += sizeof(details[i].lifetime);

            // inHand
            memcpy(copyAt, &details[i].inHand, sizeof(details[i].inHand));
            copyAt += sizeof(details[i].inHand);
            sizeOut += sizeof(details[i].inHand);

            // script
            uint16_t scriptLength = details[i].updateScript.size() + 1;
            memcpy(copyAt, &scriptLength, sizeof(scriptLength));
            copyAt += sizeof(scriptLength);
            sizeOut += sizeof(scriptLength);
            memcpy(copyAt, qPrintable(details[i].updateScript), scriptLength);
            copyAt += scriptLength;
            sizeOut += scriptLength;

            bool wantDebugging = false;
            if (wantDebugging) {
                printf("encodeParticleEditMessageDetails()....\n");
                printf("Particle id  :%u\n", details[i].id);
                printf(" nextID:%u\n", _nextID);
            }
        }
        // cleanup
        delete[] octcode;
    }

    return success;
}

// adjust any internal timestamps to fix clock skew for this server
void Particle::adjustEditPacketForClockSkew(unsigned char* codeColorBuffer, ssize_t length, int clockSkew) {
    unsigned char* dataAt = codeColorBuffer;
    int octets = numberOfThreeBitSectionsInCode(dataAt);
    int lengthOfOctcode = bytesRequiredForCodeLength(octets);
    dataAt += lengthOfOctcode;

    // id
    uint32_t id;
    memcpy(&id, dataAt, sizeof(id));
    dataAt += sizeof(id);
    // special case for handling "new" particles
    if (id == NEW_PARTICLE) {
        // If this is a NEW_PARTICLE, then we assume that there's an additional uint32_t creatorToken, that
        // we want to send back to the creator as an map to the actual id
        dataAt += sizeof(uint32_t);
    }

    // lastEdited
    uint64_t lastEditedInLocalTime;
    memcpy(&lastEditedInLocalTime, dataAt, sizeof(lastEditedInLocalTime));
    uint64_t lastEditedInServerTime = lastEditedInLocalTime + clockSkew;
    memcpy(dataAt, &lastEditedInServerTime, sizeof(lastEditedInServerTime));
    const bool wantDebug = false;
    if (wantDebug) {
        qDebug("Particle::adjustEditPacketForClockSkew()...");
        qDebug() << "     lastEditedInLocalTime: " << lastEditedInLocalTime;
        qDebug() << "                 clockSkew: " << clockSkew;
        qDebug() << "    lastEditedInServerTime: " << lastEditedInServerTime;
    }
}


void Particle::update() {
    uint64_t now = usecTimestampNow();
    float elapsed = static_cast<float>(now - _lastUpdated);
    _lastUpdated = now;
    float timeElapsed = elapsed / static_cast<float>(USECS_PER_SECOND);

    // calculate our default shouldDie state... then allow script to change it if it wants...
    float velocityScalar = glm::length(getVelocity());
    const float STILL_MOVING = 0.05f / static_cast<float>(TREE_SCALE);
    bool isStillMoving = (velocityScalar > STILL_MOVING);
    const float REALLY_OLD = 30.0f; // 30 seconds
    bool isReallyOld = (getAge() > REALLY_OLD);
    bool isInHand = getInHand();

    // Lifetime - even if you're moving, or in hand, if you're age is greater than your requested lifetime, you are going to die
    bool shouldDie = getShouldDie() || (getAge() > getLifetime()) || (!isInHand && !isStillMoving && isReallyOld);
    setShouldDie(shouldDie);

    runUpdateScript(); // allow the javascript to alter our state

    // If the ball is in hand, it doesn't move or have gravity effect it
    if (!isInHand) {
        _position += _velocity * timeElapsed;

        // handle bounces off the ground...
        if (_position.y <= 0) {
            _velocity = _velocity * glm::vec3(1,-1,1);
            _position.y = 0;
        }

        // handle gravity....
        _velocity += _gravity * timeElapsed;

        // handle damping
        glm::vec3 dampingResistance = _velocity * _damping;
        _velocity -= dampingResistance * timeElapsed;
        //printf("applying damping to Particle timeElapsed=%f\n",timeElapsed);
    }
}

void Particle::runUpdateScript() {
    if (!_script.isEmpty()) {
        ScriptEngine engine(_script); // no menu or controller interface...

        if (_voxelEditSender) {
            engine.getVoxelsScriptingInterface()->setPacketSender(_voxelEditSender);
        }
        if (_particleEditSender) {
            engine.getParticlesScriptingInterface()->setPacketSender(_particleEditSender);
        }

        // Add the Particle object
        ParticleScriptObject particleScriptable(this);
        engine.registerGlobalObject("Particle", &particleScriptable);

        // init and evaluate the script, but return so we can emit the collision
        engine.evaluate();

        particleScriptable.emitUpdate();

        // it seems like we may need to send out particle edits if the state of our particle was changed.

        if (_voxelEditSender) {
            _voxelEditSender->releaseQueuedMessages();
        }
        if (_particleEditSender) {
            _particleEditSender->releaseQueuedMessages();
        }
    }
}

void Particle::collisionWithParticle(Particle* other) {
    if (!_script.isEmpty()) {
        ScriptEngine engine(_script); // no menu or controller interface...

        if (_voxelEditSender) {
            engine.getVoxelsScriptingInterface()->setPacketSender(_voxelEditSender);
        }
        if (_particleEditSender) {
            engine.getParticlesScriptingInterface()->setPacketSender(_particleEditSender);
        }

        // Add the Particle object
        ParticleScriptObject particleScriptable(this);
        engine.registerGlobalObject("Particle", &particleScriptable);

        // init and evaluate the script, but return so we can emit the collision
        engine.evaluate();

        ParticleScriptObject otherParticleScriptable(other);
        particleScriptable.emitCollisionWithParticle(&otherParticleScriptable);

        // it seems like we may need to send out particle edits if the state of our particle was changed.

        if (_voxelEditSender) {
            _voxelEditSender->releaseQueuedMessages();
        }
        if (_particleEditSender) {
            _particleEditSender->releaseQueuedMessages();
        }
    }
}

void Particle::collisionWithVoxel(VoxelDetail* voxelDetails) {
    if (!_script.isEmpty()) {

        ScriptEngine engine(_script); // no menu or controller interface...

        // setup the packet senders and jurisdiction listeners of the script engine's scripting interfaces so
        // we can use the same ones as our context.
        if (_voxelEditSender) {
            engine.getVoxelsScriptingInterface()->setPacketSender(_voxelEditSender);
        }
        if (_particleEditSender) {
            engine.getParticlesScriptingInterface()->setPacketSender(_particleEditSender);
        }

        // Add the Particle object
        ParticleScriptObject particleScriptable(this);
        engine.registerGlobalObject("Particle", &particleScriptable);

        // init and evaluate the script, but return so we can emit the collision
        engine.evaluate();

        VoxelDetailScriptObject voxelDetailsScriptable(voxelDetails);
        particleScriptable.emitCollisionWithVoxel(&voxelDetailsScriptable);

        // it seems like we may need to send out particle edits if the state of our particle was changed.

        if (_voxelEditSender) {
            _voxelEditSender->releaseQueuedMessages();
        }
        if (_particleEditSender) {
            _particleEditSender->releaseQueuedMessages();
        }
    }
}



void Particle::setAge(float age) {
    uint64_t ageInUsecs = age * USECS_PER_SECOND;
    _created = usecTimestampNow() - ageInUsecs;
}

void Particle::copyChangedProperties(const Particle& other) {
    float age = getAge();
    *this = other;
    setAge(age);
}

ParticleProperties Particle::getProperties() const {
    ParticleProperties properties;
    properties.copyFromParticle(*this);
    return properties;
}

void Particle::setProperties(const ParticleProperties& properties) {
    properties.copyToParticle(*this);
}

ParticleProperties::ParticleProperties() :
    _position(0),
    _color(),
    _radius(DEFAULT_RADIUS),
    _velocity(0),
    _gravity(DEFAULT_GRAVITY),
    _damping(DEFAULT_DAMPING),
    _lifetime(DEFAULT_LIFETIME),
    _script(""),
    _inHand(false),
    _shouldDie(false),

    _positionChanged(false),
    _colorChanged(false),
    _radiusChanged(false),
    _velocityChanged(false),
    _gravityChanged(false),
    _dampingChanged(false),
    _lifetimeChanged(false),
    _scriptChanged(false),
    _inHandChanged(false),
    _shouldDieChanged(false)
{
}

QScriptValue ParticleProperties::copyToScriptValue(QScriptEngine* engine) const {
    QScriptValue properties = engine->newObject();

    QScriptValue position = vec3toScriptValue(engine, _position);
    properties.setProperty("position", position);

    QScriptValue color = xColorToScriptValue(engine, _color);
    properties.setProperty("color", color);

    properties.setProperty("radius", _radius);

    QScriptValue velocity = vec3toScriptValue(engine, _velocity);
    properties.setProperty("velocity", velocity);

    QScriptValue gravity = vec3toScriptValue(engine, _gravity);
    properties.setProperty("gravity", gravity);

    properties.setProperty("damping", _damping);
    properties.setProperty("lifetime", _lifetime);
    properties.setProperty("script", _script);
    properties.setProperty("inHand", _inHand);
    properties.setProperty("shouldDie", _shouldDie);

    return properties;
}

void ParticleProperties::copyFromScriptValue(const QScriptValue &object) {

    QScriptValue position = object.property("position");
    if (position.isValid()) {
        QScriptValue x = position.property("x");
        QScriptValue y = position.property("y");
        QScriptValue z = position.property("z");
        if (x.isValid() && y.isValid() && z.isValid()) {
            glm::vec3 newPosition;
            newPosition.x = x.toVariant().toFloat();
            newPosition.y = x.toVariant().toFloat();
            newPosition.z = x.toVariant().toFloat();
            if (newPosition != _position) {
                _position = newPosition;
                _positionChanged = true;
            }
        }
    }

    QScriptValue color = object.property("color");
    if (color.isValid()) {
        QScriptValue red = color.property("red");
        QScriptValue green = color.property("green");
        QScriptValue blue = color.property("blue");
        if (red.isValid() && green.isValid() && blue.isValid()) {
            xColor newColor;
            newColor.red = red.toVariant().toInt();
            newColor.green = green.toVariant().toInt();
            newColor.blue = blue.toVariant().toInt();
            if (newColor.red != _color.red ||
                newColor.green != _color.green ||
                newColor.blue != _color.blue) {
                _color = newColor;
                _colorChanged = true;
            }
        }
    }

    QScriptValue radius = object.property("radius");
    if (radius.isValid()) {
        float newRadius;
        newRadius = radius.toVariant().toFloat();
        if (newRadius != _radius) {
            _radius = newRadius;
            _radiusChanged = true;
        }
    }

    QScriptValue velocity = object.property("velocity");
    if (velocity.isValid()) {
        QScriptValue x = velocity.property("x");
        QScriptValue y = velocity.property("y");
        QScriptValue z = velocity.property("z");
        if (x.isValid() && y.isValid() && z.isValid()) {
            glm::vec3 newVelocity;
            newVelocity.x = x.toVariant().toFloat();
            newVelocity.y = x.toVariant().toFloat();
            newVelocity.z = x.toVariant().toFloat();
            if (newVelocity != _velocity) {
                _velocity = newVelocity;
                _velocityChanged = true;
            }
        }
    }

    QScriptValue gravity = object.property("gravity");
    if (gravity.isValid()) {
        QScriptValue x = gravity.property("x");
        QScriptValue y = gravity.property("y");
        QScriptValue z = gravity.property("z");
        if (x.isValid() && y.isValid() && z.isValid()) {
            glm::vec3 newGravity;
            newGravity.x = x.toVariant().toFloat();
            newGravity.y = x.toVariant().toFloat();
            newGravity.z = x.toVariant().toFloat();
            if (newGravity != _gravity) {
                _gravity = newGravity;
                _gravityChanged = true;
            }
        }
    }

    QScriptValue damping = object.property("damping");
    if (damping.isValid()) {
        float newDamping;
        newDamping = damping.toVariant().toFloat();
        if (newDamping != _damping) {
            _damping = newDamping;
            _dampingChanged = true;
        }
    }

    QScriptValue lifetime = object.property("lifetime");
    if (lifetime.isValid()) {
        float newLifetime;
        newLifetime = lifetime.toVariant().toFloat();
        if (newLifetime != _lifetime) {
            _lifetime = newLifetime;
            _lifetimeChanged = true;
        }
    }

    QScriptValue script = object.property("script");
    if (script.isValid()) {
        QString newScript;
        newScript = script.toVariant().toString();
        if (newScript != _script) {
            _script = newScript;
            _scriptChanged = true;
        }
    }

    QScriptValue inHand = object.property("inHand");
    if (inHand.isValid()) {
        bool newInHand;
        newInHand = inHand.toVariant().toBool();
        if (newInHand != _inHand) {
            _inHand = newInHand;
            _inHandChanged = true;
        }
    }

    QScriptValue shouldDie = object.property("shouldDie");
    if (shouldDie.isValid()) {
        bool newShouldDie;
        newShouldDie = shouldDie.toVariant().toBool();
        if (newShouldDie != _shouldDie) {
            _shouldDie = newShouldDie;
            _shouldDieChanged = true;
        }
    }
}

void ParticleProperties::copyToParticle(Particle& particle) const {
    if (_positionChanged) {
        particle.setPosition(_position);
    }

    if (_colorChanged) {
        particle.setColor(_color);
    }

    if (_radiusChanged) {
        particle.setRadius(_radius);
    }

    if (_velocityChanged) {
        particle.setVelocity(_velocity);
    }

    if (_gravityChanged) {
        particle.setGravity(_gravity);
    }

    if (_dampingChanged) {
        particle.setDamping(_damping);
    }

    if (_lifetimeChanged) {
        particle.setLifetime(_lifetime);
    }

    if (_scriptChanged) {
        particle.setScript(_script);
    }

    if (_inHandChanged) {
        particle.setInHand(_inHand);
    }

    if (_shouldDieChanged) {
        particle.setShouldDie(_shouldDie);
    }
}

void ParticleProperties::copyFromParticle(const Particle& particle) {
    _position = particle.getPosition();
    _color = particle.getXColor();
    _radius = particle.getRadius();
    _velocity = particle.getVelocity();
    _gravity = particle.getGravity();
    _damping = particle.getDamping();
    _lifetime = particle.getLifetime();
    _script = particle.getScript();
    _inHand = particle.getInHand();
    _shouldDie = particle.getShouldDie();

    _positionChanged = false;
    _colorChanged = false;
    _radiusChanged = false;
    _velocityChanged = false;
    _gravityChanged = false;
    _dampingChanged = false;
    _lifetimeChanged = false;
    _scriptChanged = false;
    _inHandChanged = false;
    _shouldDieChanged = false;
}

QScriptValue ParticlePropertiesToScriptValue(QScriptEngine* engine, const ParticleProperties& properties) {
    return properties.copyToScriptValue(engine);
}

void ParticlePropertiesFromScriptValue(const QScriptValue &object, ParticleProperties& properties) {
    properties.copyFromScriptValue(object);
}


QScriptValue ParticleIDtoScriptValue(QScriptEngine* engine, const ParticleID& id) {
    QScriptValue obj = engine->newObject();
    obj.setProperty("id", id.id);
    obj.setProperty("creatorTokenID", id.creatorTokenID);
    obj.setProperty("isKnownID", id.isKnownID);
    return obj;
}

void ParticleIDfromScriptValue(const QScriptValue &object, ParticleID& id) {
    id.id = object.property("id").toVariant().toUInt();
    id.creatorTokenID = object.property("creatorTokenID").toVariant().toUInt();
    id.isKnownID = object.property("isKnownID").toVariant().toBool();
}


