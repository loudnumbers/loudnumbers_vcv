
#pragma once

/**
 * Gate inputs will be considered "low" if they are below this
 */
const float cGateLow = .8f;

/**
* Gate inputs will be considered "high" if they are above this
*/
const float cGateHi = 1.6f;

const float cGateOutHi = 10.0f;

const float cGateOutLow = 0.0f;

const int TRIGGER_OUT_TIME_MS = 5;

class SchmidtTrigger
{
public:
    SchmidtTrigger(float thLo = cGateLow, float thHi = cGateHi) :
        _thLo(thLo), _thHi(thHi), _lastOut(false)
    {
    }

    bool go(float input)
    {
        if (_lastOut)		// if we were true last time
        {
            if (input < _thLo) {
                _lastOut = false;
            }
        } else {
            if (input > _thHi) {
                _lastOut = true;
            }
        }
        return _lastOut;
    }

    float thhi() const
    {
        return _thHi;
    }
    float thlo() const
    {
        return _thLo;
    }

private:
    const float _thLo;
    const float _thHi;
    bool	_lastOut;
};


