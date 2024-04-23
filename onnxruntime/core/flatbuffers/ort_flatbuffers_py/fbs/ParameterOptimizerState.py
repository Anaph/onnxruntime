# automatically generated by the FlatBuffers compiler, do not modify

# namespace: fbs

import flatbuffers
from flatbuffers.compat import import_numpy
np = import_numpy()

class ParameterOptimizerState(object):
    __slots__ = ['_tab']

    @classmethod
    def GetRootAs(cls, buf, offset=0):
        n = flatbuffers.encode.Get(flatbuffers.packer.uoffset, buf, offset)
        x = ParameterOptimizerState()
        x.Init(buf, n + offset)
        return x

    @classmethod
    def GetRootAsParameterOptimizerState(cls, buf, offset=0):
        """This method is deprecated. Please switch to GetRootAs."""
        return cls.GetRootAs(buf, offset)
    @classmethod
    def ParameterOptimizerStateBufferHasIdentifier(cls, buf, offset, size_prefixed=False):
        return flatbuffers.util.BufferHasIdentifier(buf, offset, b"\x4F\x44\x54\x43", size_prefixed=size_prefixed)

    # ParameterOptimizerState
    def Init(self, buf, pos):
        self._tab = flatbuffers.table.Table(buf, pos)

    # ParameterOptimizerState
    def ParamName(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(4))
        if o != 0:
            return self._tab.String(o + self._tab.Pos)
        return None

    # ParameterOptimizerState
    def Momentums(self, j):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            x = self._tab.Vector(o)
            x += flatbuffers.number_types.UOffsetTFlags.py_type(j) * 4
            x = self._tab.Indirect(x)
            from ort_flatbuffers_py.fbs.Tensor import Tensor
            obj = Tensor()
            obj.Init(self._tab.Bytes, x)
            return obj
        return None

    # ParameterOptimizerState
    def MomentumsLength(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            return self._tab.VectorLen(o)
        return 0

    # ParameterOptimizerState
    def MomentumsIsNone(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        return o == 0

def ParameterOptimizerStateStart(builder):
    builder.StartObject(2)

def Start(builder):
    ParameterOptimizerStateStart(builder)

def ParameterOptimizerStateAddParamName(builder, paramName):
    builder.PrependUOffsetTRelativeSlot(0, flatbuffers.number_types.UOffsetTFlags.py_type(paramName), 0)

def AddParamName(builder, paramName):
    ParameterOptimizerStateAddParamName(builder, paramName)

def ParameterOptimizerStateAddMomentums(builder, momentums):
    builder.PrependUOffsetTRelativeSlot(1, flatbuffers.number_types.UOffsetTFlags.py_type(momentums), 0)

def AddMomentums(builder, momentums):
    ParameterOptimizerStateAddMomentums(builder, momentums)

def ParameterOptimizerStateStartMomentumsVector(builder, numElems):
    return builder.StartVector(4, numElems, 4)

def StartMomentumsVector(builder, numElems: int) -> int:
    return ParameterOptimizerStateStartMomentumsVector(builder, numElems)

def ParameterOptimizerStateEnd(builder):
    return builder.EndObject()

def End(builder):
    return ParameterOptimizerStateEnd(builder)
