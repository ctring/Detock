# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: proto/configuration.proto
"""Generated protocol buffer code."""
from google.protobuf.internal import enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from google.protobuf import reflection as _reflection
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from proto import modules_pb2 as proto_dot_modules__pb2
from proto import transaction_pb2 as proto_dot_transaction__pb2


DESCRIPTOR = _descriptor.FileDescriptor(
  name='proto/configuration.proto',
  package='slog.internal',
  syntax='proto3',
  serialized_options=None,
  create_key=_descriptor._internal_create_key,
  serialized_pb=b'\n\x19proto/configuration.proto\x12\rslog.internal\x1a\x13proto/modules.proto\x1a\x17proto/transaction.proto\"j\n\x07Replica\x12\x11\n\taddresses\x18\x01 \x03(\t\x12\x18\n\x10public_addresses\x18\x02 \x03(\t\x12\x18\n\x10\x63lient_addresses\x18\x03 \x03(\t\x12\x18\n\x10\x64istance_ranking\x18\x04 \x01(\t\"H\n\x1aReplicationDelayExperiment\x12\x11\n\tdelay_pct\x18\x01 \x01(\r\x12\x17\n\x0f\x64\x65lay_amount_ms\x18\x02 \x01(\r\"3\n\x10HashPartitioning\x12\x1f\n\x17partition_key_num_bytes\x18\x01 \x01(\r\"D\n\x12SimplePartitioning\x12\x13\n\x0bnum_records\x18\x01 \x01(\x04\x12\x19\n\x11record_size_bytes\x18\x02 \x01(\r\"&\n\x10TPCCPartitioning\x12\x12\n\nwarehouses\x18\x01 \x01(\x05\"9\n\nCpuPinning\x12\x1e\n\x06module\x18\x01 \x01(\x0e\x32\x0e.slog.ModuleId\x12\x0b\n\x03\x63pu\x18\x02 \x01(\r\"\xa3\x07\n\rConfiguration\x12\x10\n\x08protocol\x18\x01 \x01(\t\x12(\n\x08replicas\x18\x02 \x03(\x0b\x32\x16.slog.internal.Replica\x12\x14\n\x0c\x62roker_ports\x18\x03 \x03(\r\x12\x13\n\x0bserver_port\x18\x04 \x01(\r\x12\x16\n\x0e\x66orwarder_port\x18\x05 \x01(\r\x12\x16\n\x0esequencer_port\x18\x06 \x01(\r\x12\x16\n\x0enum_partitions\x18\x07 \x01(\r\x12<\n\x11hash_partitioning\x18\x08 \x01(\x0b\x32\x1f.slog.internal.HashPartitioningH\x00\x12@\n\x13simple_partitioning\x18\t \x01(\x0b\x32!.slog.internal.SimplePartitioningH\x00\x12<\n\x11tpcc_partitioning\x18\n \x01(\x0b\x32\x1f.slog.internal.TPCCPartitioningH\x00\x12\x13\n\x0bnum_workers\x18\x0b \x01(\r\x12 \n\x18\x66orwarder_batch_duration\x18\x0c \x01(\x04\x12 \n\x18sequencer_batch_duration\x18\r \x01(\x04\x12\x1a\n\x12replication_factor\x18\x0e \x01(\r\x12\x19\n\x11replication_order\x18\x0f \x03(\t\x12\x44\n\x11replication_delay\x18\x10 \x01(\x0b\x32).slog.internal.ReplicationDelayExperiment\x12.\n\x0e\x65nabled_events\x18\x11 \x03(\x0e\x32\x16.slog.TransactionEvent\x12\x19\n\x11\x62ypass_mh_orderer\x18\x12 \x01(\x08\x12/\n\x0c\x63pu_pinnings\x18\x13 \x03(\x0b\x32\x19.slog.internal.CpuPinning\x12\x18\n\x10return_dummy_txn\x18\x14 \x01(\x08\x12\x14\n\x0crecv_retries\x18\x15 \x01(\x05\x12\x34\n\x0e\x65xecution_type\x18\x16 \x01(\x0e\x32\x1c.slog.internal.ExecutionType\x12\x1d\n\x15synchronized_batching\x18\x17 \x01(\x08\x12\x13\n\x0bsample_rate\x18\x18 \x01(\r\x12)\n!interleaver_remote_to_local_ratio\x18\x19 \x01(\tB\x0e\n\x0cpartitioning*3\n\rExecutionType\x12\r\n\tKEY_VALUE\x10\x00\x12\x08\n\x04NOOP\x10\x01\x12\t\n\x05TPC_C\x10\x02\x62\x06proto3'
  ,
  dependencies=[proto_dot_modules__pb2.DESCRIPTOR,proto_dot_transaction__pb2.DESCRIPTOR,])

_EXECUTIONTYPE = _descriptor.EnumDescriptor(
  name='ExecutionType',
  full_name='slog.internal.ExecutionType',
  filename=None,
  file=DESCRIPTOR,
  create_key=_descriptor._internal_create_key,
  values=[
    _descriptor.EnumValueDescriptor(
      name='KEY_VALUE', index=0, number=0,
      serialized_options=None,
      type=None,
      create_key=_descriptor._internal_create_key),
    _descriptor.EnumValueDescriptor(
      name='NOOP', index=1, number=1,
      serialized_options=None,
      type=None,
      create_key=_descriptor._internal_create_key),
    _descriptor.EnumValueDescriptor(
      name='TPC_C', index=2, number=2,
      serialized_options=None,
      type=None,
      create_key=_descriptor._internal_create_key),
  ],
  containing_type=None,
  serialized_options=None,
  serialized_start=1428,
  serialized_end=1479,
)
_sym_db.RegisterEnumDescriptor(_EXECUTIONTYPE)

ExecutionType = enum_type_wrapper.EnumTypeWrapper(_EXECUTIONTYPE)
KEY_VALUE = 0
NOOP = 1
TPC_C = 2



_REPLICA = _descriptor.Descriptor(
  name='Replica',
  full_name='slog.internal.Replica',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='addresses', full_name='slog.internal.Replica.addresses', index=0,
      number=1, type=9, cpp_type=9, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='public_addresses', full_name='slog.internal.Replica.public_addresses', index=1,
      number=2, type=9, cpp_type=9, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='client_addresses', full_name='slog.internal.Replica.client_addresses', index=2,
      number=3, type=9, cpp_type=9, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='distance_ranking', full_name='slog.internal.Replica.distance_ranking', index=3,
      number=4, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=b"".decode('utf-8'),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=90,
  serialized_end=196,
)


_REPLICATIONDELAYEXPERIMENT = _descriptor.Descriptor(
  name='ReplicationDelayExperiment',
  full_name='slog.internal.ReplicationDelayExperiment',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='delay_pct', full_name='slog.internal.ReplicationDelayExperiment.delay_pct', index=0,
      number=1, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='delay_amount_ms', full_name='slog.internal.ReplicationDelayExperiment.delay_amount_ms', index=1,
      number=2, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=198,
  serialized_end=270,
)


_HASHPARTITIONING = _descriptor.Descriptor(
  name='HashPartitioning',
  full_name='slog.internal.HashPartitioning',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='partition_key_num_bytes', full_name='slog.internal.HashPartitioning.partition_key_num_bytes', index=0,
      number=1, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=272,
  serialized_end=323,
)


_SIMPLEPARTITIONING = _descriptor.Descriptor(
  name='SimplePartitioning',
  full_name='slog.internal.SimplePartitioning',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='num_records', full_name='slog.internal.SimplePartitioning.num_records', index=0,
      number=1, type=4, cpp_type=4, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='record_size_bytes', full_name='slog.internal.SimplePartitioning.record_size_bytes', index=1,
      number=2, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=325,
  serialized_end=393,
)


_TPCCPARTITIONING = _descriptor.Descriptor(
  name='TPCCPartitioning',
  full_name='slog.internal.TPCCPartitioning',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='warehouses', full_name='slog.internal.TPCCPartitioning.warehouses', index=0,
      number=1, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=395,
  serialized_end=433,
)


_CPUPINNING = _descriptor.Descriptor(
  name='CpuPinning',
  full_name='slog.internal.CpuPinning',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='module', full_name='slog.internal.CpuPinning.module', index=0,
      number=1, type=14, cpp_type=8, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='cpu', full_name='slog.internal.CpuPinning.cpu', index=1,
      number=2, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=435,
  serialized_end=492,
)


_CONFIGURATION = _descriptor.Descriptor(
  name='Configuration',
  full_name='slog.internal.Configuration',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  create_key=_descriptor._internal_create_key,
  fields=[
    _descriptor.FieldDescriptor(
      name='protocol', full_name='slog.internal.Configuration.protocol', index=0,
      number=1, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=b"".decode('utf-8'),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='replicas', full_name='slog.internal.Configuration.replicas', index=1,
      number=2, type=11, cpp_type=10, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='broker_ports', full_name='slog.internal.Configuration.broker_ports', index=2,
      number=3, type=13, cpp_type=3, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='server_port', full_name='slog.internal.Configuration.server_port', index=3,
      number=4, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='forwarder_port', full_name='slog.internal.Configuration.forwarder_port', index=4,
      number=5, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='sequencer_port', full_name='slog.internal.Configuration.sequencer_port', index=5,
      number=6, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='num_partitions', full_name='slog.internal.Configuration.num_partitions', index=6,
      number=7, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='hash_partitioning', full_name='slog.internal.Configuration.hash_partitioning', index=7,
      number=8, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='simple_partitioning', full_name='slog.internal.Configuration.simple_partitioning', index=8,
      number=9, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='tpcc_partitioning', full_name='slog.internal.Configuration.tpcc_partitioning', index=9,
      number=10, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='num_workers', full_name='slog.internal.Configuration.num_workers', index=10,
      number=11, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='forwarder_batch_duration', full_name='slog.internal.Configuration.forwarder_batch_duration', index=11,
      number=12, type=4, cpp_type=4, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='sequencer_batch_duration', full_name='slog.internal.Configuration.sequencer_batch_duration', index=12,
      number=13, type=4, cpp_type=4, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='replication_factor', full_name='slog.internal.Configuration.replication_factor', index=13,
      number=14, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='replication_order', full_name='slog.internal.Configuration.replication_order', index=14,
      number=15, type=9, cpp_type=9, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='replication_delay', full_name='slog.internal.Configuration.replication_delay', index=15,
      number=16, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='enabled_events', full_name='slog.internal.Configuration.enabled_events', index=16,
      number=17, type=14, cpp_type=8, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='bypass_mh_orderer', full_name='slog.internal.Configuration.bypass_mh_orderer', index=17,
      number=18, type=8, cpp_type=7, label=1,
      has_default_value=False, default_value=False,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='cpu_pinnings', full_name='slog.internal.Configuration.cpu_pinnings', index=18,
      number=19, type=11, cpp_type=10, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='return_dummy_txn', full_name='slog.internal.Configuration.return_dummy_txn', index=19,
      number=20, type=8, cpp_type=7, label=1,
      has_default_value=False, default_value=False,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='recv_retries', full_name='slog.internal.Configuration.recv_retries', index=20,
      number=21, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='execution_type', full_name='slog.internal.Configuration.execution_type', index=21,
      number=22, type=14, cpp_type=8, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='synchronized_batching', full_name='slog.internal.Configuration.synchronized_batching', index=22,
      number=23, type=8, cpp_type=7, label=1,
      has_default_value=False, default_value=False,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='sample_rate', full_name='slog.internal.Configuration.sample_rate', index=23,
      number=24, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
    _descriptor.FieldDescriptor(
      name='interleaver_remote_to_local_ratio', full_name='slog.internal.Configuration.interleaver_remote_to_local_ratio', index=24,
      number=25, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=b"".decode('utf-8'),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR,  create_key=_descriptor._internal_create_key),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
    _descriptor.OneofDescriptor(
      name='partitioning', full_name='slog.internal.Configuration.partitioning',
      index=0, containing_type=None,
      create_key=_descriptor._internal_create_key,
    fields=[]),
  ],
  serialized_start=495,
  serialized_end=1426,
)

_CPUPINNING.fields_by_name['module'].enum_type = proto_dot_modules__pb2._MODULEID
_CONFIGURATION.fields_by_name['replicas'].message_type = _REPLICA
_CONFIGURATION.fields_by_name['hash_partitioning'].message_type = _HASHPARTITIONING
_CONFIGURATION.fields_by_name['simple_partitioning'].message_type = _SIMPLEPARTITIONING
_CONFIGURATION.fields_by_name['tpcc_partitioning'].message_type = _TPCCPARTITIONING
_CONFIGURATION.fields_by_name['replication_delay'].message_type = _REPLICATIONDELAYEXPERIMENT
_CONFIGURATION.fields_by_name['enabled_events'].enum_type = proto_dot_transaction__pb2._TRANSACTIONEVENT
_CONFIGURATION.fields_by_name['cpu_pinnings'].message_type = _CPUPINNING
_CONFIGURATION.fields_by_name['execution_type'].enum_type = _EXECUTIONTYPE
_CONFIGURATION.oneofs_by_name['partitioning'].fields.append(
  _CONFIGURATION.fields_by_name['hash_partitioning'])
_CONFIGURATION.fields_by_name['hash_partitioning'].containing_oneof = _CONFIGURATION.oneofs_by_name['partitioning']
_CONFIGURATION.oneofs_by_name['partitioning'].fields.append(
  _CONFIGURATION.fields_by_name['simple_partitioning'])
_CONFIGURATION.fields_by_name['simple_partitioning'].containing_oneof = _CONFIGURATION.oneofs_by_name['partitioning']
_CONFIGURATION.oneofs_by_name['partitioning'].fields.append(
  _CONFIGURATION.fields_by_name['tpcc_partitioning'])
_CONFIGURATION.fields_by_name['tpcc_partitioning'].containing_oneof = _CONFIGURATION.oneofs_by_name['partitioning']
DESCRIPTOR.message_types_by_name['Replica'] = _REPLICA
DESCRIPTOR.message_types_by_name['ReplicationDelayExperiment'] = _REPLICATIONDELAYEXPERIMENT
DESCRIPTOR.message_types_by_name['HashPartitioning'] = _HASHPARTITIONING
DESCRIPTOR.message_types_by_name['SimplePartitioning'] = _SIMPLEPARTITIONING
DESCRIPTOR.message_types_by_name['TPCCPartitioning'] = _TPCCPARTITIONING
DESCRIPTOR.message_types_by_name['CpuPinning'] = _CPUPINNING
DESCRIPTOR.message_types_by_name['Configuration'] = _CONFIGURATION
DESCRIPTOR.enum_types_by_name['ExecutionType'] = _EXECUTIONTYPE
_sym_db.RegisterFileDescriptor(DESCRIPTOR)

Replica = _reflection.GeneratedProtocolMessageType('Replica', (_message.Message,), {
  'DESCRIPTOR' : _REPLICA,
  '__module__' : 'proto.configuration_pb2'
  # @@protoc_insertion_point(class_scope:slog.internal.Replica)
  })
_sym_db.RegisterMessage(Replica)

ReplicationDelayExperiment = _reflection.GeneratedProtocolMessageType('ReplicationDelayExperiment', (_message.Message,), {
  'DESCRIPTOR' : _REPLICATIONDELAYEXPERIMENT,
  '__module__' : 'proto.configuration_pb2'
  # @@protoc_insertion_point(class_scope:slog.internal.ReplicationDelayExperiment)
  })
_sym_db.RegisterMessage(ReplicationDelayExperiment)

HashPartitioning = _reflection.GeneratedProtocolMessageType('HashPartitioning', (_message.Message,), {
  'DESCRIPTOR' : _HASHPARTITIONING,
  '__module__' : 'proto.configuration_pb2'
  # @@protoc_insertion_point(class_scope:slog.internal.HashPartitioning)
  })
_sym_db.RegisterMessage(HashPartitioning)

SimplePartitioning = _reflection.GeneratedProtocolMessageType('SimplePartitioning', (_message.Message,), {
  'DESCRIPTOR' : _SIMPLEPARTITIONING,
  '__module__' : 'proto.configuration_pb2'
  # @@protoc_insertion_point(class_scope:slog.internal.SimplePartitioning)
  })
_sym_db.RegisterMessage(SimplePartitioning)

TPCCPartitioning = _reflection.GeneratedProtocolMessageType('TPCCPartitioning', (_message.Message,), {
  'DESCRIPTOR' : _TPCCPARTITIONING,
  '__module__' : 'proto.configuration_pb2'
  # @@protoc_insertion_point(class_scope:slog.internal.TPCCPartitioning)
  })
_sym_db.RegisterMessage(TPCCPartitioning)

CpuPinning = _reflection.GeneratedProtocolMessageType('CpuPinning', (_message.Message,), {
  'DESCRIPTOR' : _CPUPINNING,
  '__module__' : 'proto.configuration_pb2'
  # @@protoc_insertion_point(class_scope:slog.internal.CpuPinning)
  })
_sym_db.RegisterMessage(CpuPinning)

Configuration = _reflection.GeneratedProtocolMessageType('Configuration', (_message.Message,), {
  'DESCRIPTOR' : _CONFIGURATION,
  '__module__' : 'proto.configuration_pb2'
  # @@protoc_insertion_point(class_scope:slog.internal.Configuration)
  })
_sym_db.RegisterMessage(Configuration)


# @@protoc_insertion_point(module_scope)
