/** @file
 *  @brief Bluetooth Mesh Configuration Client Model APIs.
 */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_BLUETOOTH_MESH_CFG_CLI_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_MESH_CFG_CLI_H_

/**
 * @brief Bluetooth Mesh
 * @defgroup bt_mesh_cfg_cli Bluetooth Mesh Configuration Client Model
 * @ingroup bt_mesh
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Mesh Configuration Client Model Context */
struct bt_mesh_cfg_cli {
	/** Composition data model entry pointer. */
	struct bt_mesh_model *model;

	/* Internal parameters for tracking message responses. */
	struct k_sem          op_sync;
	u32_t                 op_pending;
	void                 *op_param;
};

/** @def BT_MESH_MODEL_CFG_CLI
 *
 *  @brief Generic Configuration Client model composition data entry.
 *
 *  @param cli_data Pointer to a @ref bt_mesh_cfg_cli instance.
 */
#define BT_MESH_MODEL_CFG_CLI(cli_data)                                        \
	BT_MESH_MODEL_CB(BT_MESH_MODEL_ID_CFG_CLI, bt_mesh_cfg_cli_op, NULL,   \
			 cli_data, &bt_mesh_cfg_cli_cb)

/** @brief Get the target node's composition data.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param page    Composition data page, or 0xff to request the first available
 *                 page.
 *  @param status  Status response parameter.
 *  @param comp    Composition data buffer to fill.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_comp_data_get(u16_t net_idx, u16_t addr, u8_t page,
			      u8_t *status, struct net_buf_simple *comp);

/** @brief Get the target node's network beacon state.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param status  Status response parameter, returns one of
 *                 @ref BT_MESH_BEACON_DISABLED or @ref BT_MESH_BEACON_ENABLED
 *                 on success.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_beacon_get(u16_t net_idx, u16_t addr, u8_t *status);

/** @brief Set the target node's network beacon state.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param val     New network beacon state, should be one of
 *                 @ref BT_MESH_BEACON_DISABLED or @ref BT_MESH_BEACON_ENABLED.
 *  @param status  Status response parameter. Returns one of
 *                 @ref BT_MESH_BEACON_DISABLED or @ref BT_MESH_BEACON_ENABLED
 *                 on success.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_beacon_set(u16_t net_idx, u16_t addr, u8_t val, u8_t *status);

/** @brief Get the target node's Time To Live value.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param ttl     TTL response buffer.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_ttl_get(u16_t net_idx, u16_t addr, u8_t *ttl);

/** @brief Set the target node's Time To Live value.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param val     New Time To Live value.
 *  @param ttl     TTL response buffer.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_ttl_set(u16_t net_idx, u16_t addr, u8_t val, u8_t *ttl);

/** @brief Get the target node's Friend feature status.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param status  Status response parameter. Returns one of
 *                 @ref BT_MESH_FRIEND_DISABLED, @ref BT_MESH_FRIEND_ENABLED or
 *                 @ref BT_MESH_FRIEND_NOT_SUPPORTED on success.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_friend_get(u16_t net_idx, u16_t addr, u8_t *status);

/** @brief Set the target node's Friend feature state.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param val     New Friend feature state. Should be one of
 *                 @ref BT_MESH_FRIEND_DISABLED or
 *                 @ref BT_MESH_FRIEND_ENABLED.
 *  @param status  Status response parameter. Returns one of
 *                 @ref BT_MESH_FRIEND_DISABLED, @ref BT_MESH_FRIEND_ENABLED or
 *                 @ref BT_MESH_FRIEND_NOT_SUPPORTED on success.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_friend_set(u16_t net_idx, u16_t addr, u8_t val, u8_t *status);

/** @brief Get the target node's Proxy feature state.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param status  Status response parameter. Returns one of
 *                 @ref BT_MESH_GATT_PROXY_DISABLED,
 *                 @ref BT_MESH_GATT_PROXY_ENABLED or
 *                 @ref BT_MESH_GATT_PROXY_NOT_SUPPORTED on success.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_gatt_proxy_get(u16_t net_idx, u16_t addr, u8_t *status);

/** @brief Set the target node's Proxy feature state.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param val     New Proxy feature state. Must be one of
 *                 @ref BT_MESH_GATT_PROXY_DISABLED or
 *                 @ref BT_MESH_GATT_PROXY_ENABLED.
 *  @param status  Status response parameter. Returns one of
 *                 @ref BT_MESH_GATT_PROXY_DISABLED,
 *                 @ref BT_MESH_GATT_PROXY_ENABLED or
 *                 @ref BT_MESH_GATT_PROXY_NOT_SUPPORTED on success.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_gatt_proxy_set(u16_t net_idx, u16_t addr, u8_t val,
			       u8_t *status);

/** @brief Get the target node's Relay feature state.
 *
 *  @param net_idx  Network index to encrypt with.
 *  @param addr     Target node address.
 *  @param status   Status response parameter. Returns one of
 *                  @ref BT_MESH_RELAY_DISABLED, @ref BT_MESH_RELAY_ENABLED or
 *                  @ref BT_MESH_RELAY_NOT_SUPPORTED on success.
 *  @param transmit Transmit response parameter. Returns the encoded relay
 *                  transmission parameters on success. Decoded with
 *                  @ref BT_MESH_TRANSMIT_COUNT and @ref BT_MESH_TRANSMIT_INT.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_relay_get(u16_t net_idx, u16_t addr, u8_t *status,
			  u8_t *transmit);

/** @brief Set the target node's Relay parameters.
 *
 *  @param net_idx      Network index to encrypt with.
 *  @param addr         Target node address.
 *  @param new_relay    New relay state. Must be one of
 *                      @ref BT_MESH_RELAY_DISABLED or
 *                      @ref BT_MESH_RELAY_ENABLED.
 *  @param new_transmit New encoded relay transmit parameters.
 *                      @see BT_MESH_TRANSMIT.
 *  @param status       Status response parameter. Returns one of
 *                      @ref BT_MESH_RELAY_DISABLED, @ref BT_MESH_RELAY_ENABLED
 *                      or @ref BT_MESH_RELAY_NOT_SUPPORTED on success.
 *  @param transmit     Transmit response parameter. Returns the encoded relay
 *                      transmission parameters on success. Decoded with
 *                      @ref BT_MESH_TRANSMIT_COUNT and
 *                      @ref BT_MESH_TRANSMIT_INT.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_relay_set(u16_t net_idx, u16_t addr, u8_t new_relay,
			  u8_t new_transmit, u8_t *status, u8_t *transmit);

/** @brief Add a network key to the target node.
 *
 *  @param net_idx     Network index to encrypt with.
 *  @param addr        Target node address.
 *  @param key_net_idx Network key index.
 *  @param net_key     Network key.
 *  @param status      Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_net_key_add(u16_t net_idx, u16_t addr, u16_t key_net_idx,
			    const u8_t net_key[16], u8_t *status);

/** @brief Add an application key to the target node.
 *
 *  @param net_idx     Network index to encrypt with.
 *  @param addr        Target node address.
 *  @param key_net_idx Network key index the application key belongs to.
 *  @param key_app_idx Application key index.
 *  @param app_key     Application key.
 *  @param status      Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_app_key_add(u16_t net_idx, u16_t addr, u16_t key_net_idx,
			    u16_t key_app_idx, const u8_t app_key[16],
			    u8_t *status);

/** @brief Bind an application to a SIG model on the target node.
 *
 *  @param net_idx     Network index to encrypt with.
 *  @param addr        Target node address.
 *  @param elem_addr   Element address the model is in.
 *  @param mod_app_idx Application index to bind.
 *  @param mod_id      Model ID.
 *  @param status      Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_app_bind(u16_t net_idx, u16_t addr, u16_t elem_addr,
			     u16_t mod_app_idx, u16_t mod_id, u8_t *status);

/** @brief Bind an application to a vendor model on the target node.
 *
 *  @param net_idx     Network index to encrypt with.
 *  @param addr        Target node address.
 *  @param elem_addr   Element address the model is in.
 *  @param mod_app_idx Application index to bind.
 *  @param mod_id      Model ID.
 *  @param cid         Company ID of the model.
 *  @param status      Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_app_bind_vnd(u16_t net_idx, u16_t addr, u16_t elem_addr,
				 u16_t mod_app_idx, u16_t mod_id, u16_t cid,
				 u8_t *status);

/** @def BT_MESH_PUB_PERIOD_100MS
 *
 *  @brief Helper macro to encode model publication period in units of 100ms
 *
 *  @param steps Number of 100ms steps.
 *
 *  @return Encoded value that can be assigned to bt_mesh_cfg_mod_pub.period
 */
#define BT_MESH_PUB_PERIOD_100MS(steps)  ((steps) & BIT_MASK(6))

/** @def BT_MESH_PUB_PERIOD_SEC
 *
 *  @brief Helper macro to encode model publication period in units of 1 second
 *
 *  @param steps Number of 1 second steps.
 *
 *  @return Encoded value that can be assigned to bt_mesh_cfg_mod_pub.period
 */
#define BT_MESH_PUB_PERIOD_SEC(steps)   (((steps) & BIT_MASK(6)) | (1 << 6))

/** @def BT_MESH_PUB_PERIOD_10SEC
 *
 *  @brief Helper macro to encode model publication period in units of 10
 *  seconds
 *
 *  @param steps Number of 10 second steps.
 *
 *  @return Encoded value that can be assigned to bt_mesh_cfg_mod_pub.period
 */
#define BT_MESH_PUB_PERIOD_10SEC(steps) (((steps) & BIT_MASK(6)) | (2 << 6))

/** @def BT_MESH_PUB_PERIOD_10MIN
 *
 *  @brief Helper macro to encode model publication period in units of 10
 *  minutes
 *
 *  @param steps Number of 10 minute steps.
 *
 *  @return Encoded value that can be assigned to bt_mesh_cfg_mod_pub.period
 */
#define BT_MESH_PUB_PERIOD_10MIN(steps) (((steps) & BIT_MASK(6)) | (3 << 6))

/** Model publication configuration parameters. */
struct bt_mesh_cfg_mod_pub {
	/** Publication destination address. */
	u16_t  addr;
	/** Application index to publish with. */
	u16_t  app_idx;
	/** Friendship credential flag. */
	bool   cred_flag;
	/** Time To Live to publish with. */
	u8_t   ttl;
	/**
	 * Encoded publish period.
	 * @see BT_MESH_PUB_PERIOD_100MS, BT_MESH_PUB_PERIOD_SEC,
	 * BT_MESH_PUB_PERIOD_10SEC,
	 * BT_MESH_PUB_PERIOD_10MIN
	 */
	u8_t   period;
	/**
	 * Encoded transmit parameters.
	 * @see BT_MESH_TRANSMIT
	 */
	u8_t   transmit;
};

/** @brief Get publish parameters for a SIG model on the target node.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param mod_id    Model ID.
 *  @param pub       Publication parameter return buffer.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_pub_get(u16_t net_idx, u16_t addr, u16_t elem_addr,
			    u16_t mod_id, struct bt_mesh_cfg_mod_pub *pub,
			    u8_t *status);

/** @brief Get publish parameters for a vendor model on the target node.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param pub       Publication parameter return buffer.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_pub_get_vnd(u16_t net_idx, u16_t addr, u16_t elem_addr,
				u16_t mod_id, u16_t cid,
				struct bt_mesh_cfg_mod_pub *pub, u8_t *status);

/** @brief Set publish parameters for a SIG model on the target node.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param mod_id    Model ID.
 *  @param pub       Publication parameters.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_pub_set(u16_t net_idx, u16_t addr, u16_t elem_addr,
			    u16_t mod_id, struct bt_mesh_cfg_mod_pub *pub,
			    u8_t *status);

/** @brief Set publish parameters for a vendor model on the target node.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param pub       Publication parameters.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_pub_set_vnd(u16_t net_idx, u16_t addr, u16_t elem_addr,
				u16_t mod_id, u16_t cid,
				struct bt_mesh_cfg_mod_pub *pub, u8_t *status);

/** @brief Add a group address to a SIG model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param sub_addr  Group address to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_add(u16_t net_idx, u16_t addr, u16_t elem_addr,
			    u16_t sub_addr, u16_t mod_id, u8_t *status);

/** @brief Add a group address to a vendor model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param sub_addr  Group address to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_add_vnd(u16_t net_idx, u16_t addr, u16_t elem_addr,
				 u16_t sub_addr, u16_t mod_id, u16_t cid,
				 u8_t *status);

/** @brief Delete a group address in a SIG model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param sub_addr  Group address to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_del(u16_t net_idx, u16_t addr, u16_t elem_addr,
			    u16_t sub_addr, u16_t mod_id, u8_t *status);

/** @brief Delete a group address in a vendor model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param sub_addr  Group address to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_del_vnd(u16_t net_idx, u16_t addr, u16_t elem_addr,
				 u16_t sub_addr, u16_t mod_id, u16_t cid,
				 u8_t *status);

/** @brief Overwrite all addresses in a SIG model's subscription list with a
 * group address.
 *
 * Deletes all subscriptions in the model's subscription list, and adds a
 * single group address instead.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param sub_addr  Group address to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_overwrite(u16_t net_idx, u16_t addr, u16_t elem_addr,
				  u16_t sub_addr, u16_t mod_id, u8_t *status);

/** @brief Overwrite all addresses in a vendor model's subscription list with a
 * group address.
 *
 * Deletes all subscriptions in the model's subscription list, and adds a
 * single group address instead.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param sub_addr  Group address to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_overwrite_vnd(u16_t net_idx, u16_t addr,
				      u16_t elem_addr, u16_t sub_addr,
				      u16_t mod_id, u16_t cid, u8_t *status);

/** @brief Add a virtual address to a SIG model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param label     Virtual address label to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param virt_addr Virtual address response parameter.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_va_add(u16_t net_idx, u16_t addr, u16_t elem_addr,
			       const u8_t label[16], u16_t mod_id,
			       u16_t *virt_addr, u8_t *status);

/** @brief Add a virtual address to a vendor model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param label     Virtual address label to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param virt_addr Virtual address response parameter.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_va_add_vnd(u16_t net_idx, u16_t addr, u16_t elem_addr,
				   const u8_t label[16], u16_t mod_id,
				   u16_t cid, u16_t *virt_addr, u8_t *status);

/** @brief Delete a virtual address in a SIG model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param label     Virtual address parameter to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param virt_addr Virtual address response parameter.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_va_del(u16_t net_idx, u16_t addr, u16_t elem_addr,
			       const u8_t label[16], u16_t mod_id,
			       u16_t *virt_addr, u8_t *status);

/** @brief Delete a virtual address in a vendor model's subscription list.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param label     Virtual address label to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param virt_addr Virtual address response parameter.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_va_del_vnd(u16_t net_idx, u16_t addr, u16_t elem_addr,
				   const u8_t label[16], u16_t mod_id,
				   u16_t cid, u16_t *virt_addr, u8_t *status);

/** @brief Overwrite all addresses in a SIG model's subscription list with a
 * virtual address.
 *
 * Deletes all subscriptions in the model's subscription list, and adds a
 * single group address instead.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param label     Virtual address label to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param virt_addr Virtual address response parameter.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_va_overwrite(u16_t net_idx, u16_t addr,
				     u16_t elem_addr, const u8_t label[16],
				     u16_t mod_id, u16_t *virt_addr,
				     u8_t *status);

/** @brief Overwrite all addresses in a vendor model's subscription list with a
 * virtual address.
 *
 * Deletes all subscriptions in the model's subscription list, and adds a
 * single group address instead.
 *
 *  @param net_idx   Network index to encrypt with.
 *  @param addr      Target node address.
 *  @param elem_addr Element address the model is in.
 *  @param label     Virtual address label to add to the subscription list.
 *  @param mod_id    Model ID.
 *  @param cid       Company ID of the model.
 *  @param virt_addr Virtual address response parameter.
 *  @param status    Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_mod_sub_va_overwrite_vnd(u16_t net_idx, u16_t addr,
					 u16_t elem_addr, const u8_t label[16],
					 u16_t mod_id, u16_t cid,
					 u16_t *virt_addr, u8_t *status);

/** Heartbeat subscription configuration parameters. */
struct bt_mesh_cfg_hb_sub {
	/** Source address to receive Heartbeat messages from. */
	u16_t src;
	/** Destination address to receive Heartbeat messages on. */
	u16_t dst;
	/**
	 * Logarithmic subscription period to keep listening for.
	 * The decoded subscription period is (1 << (period - 1)), or 0 if
	 * period is 0.
	 */
	u8_t  period;
	/**
	 * Logarithmic Heartbeat subscription receive count.
	 * The decoded Heartbeat count is (1 << (count - 1)) if count is
	 * between 1 and 0xfe, 0 if count is 0 and 0xffff if count is 0xff.
	 *
	 * Ignored in Heartbeat subscription set.
	 */
	u8_t  count;
	/**
	 * Minimum hops in received messages, ie the shortest registered path
	 * from the publishing node to the subscribing node. A Heartbeat
	 * received from an immediate neighbor has hop count = 1.
	 *
	 * Ignored in Heartbeat subscription set.
	 */
	u8_t  min;
	/**
	 * Maximum hops in received messages, ie the longest registered path
	 * from the publishing node to the subscribing node. A Heartbeat
	 * received from an immediate neighbor has hop count = 1.
	 *
	 * Ignored in Heartbeat subscription set.
	 */
	u8_t  max;
};

/** @brief Set the target node's Heartbeat subscription parameters.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param sub     New Heartbeat subscription parameters.
 *  @param status  Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_hb_sub_set(u16_t net_idx, u16_t addr,
			   struct bt_mesh_cfg_hb_sub *sub, u8_t *status);

/** @brief Get the target node's Heartbeta subscription parameters.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param sub     Heartbeat subscription parameter return buffer.
 *  @param status  Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_hb_sub_get(u16_t net_idx, u16_t addr,
			   struct bt_mesh_cfg_hb_sub *sub, u8_t *status);

/** Heartbeat publication configuration parameters. */
struct bt_mesh_cfg_hb_pub {
	/** Heartbeat destination address. */
	u16_t dst;
	/**
	 * Logarithmic Heartbeat count. Decoded as (1 << (count - 1)) if count
	 * is between 1 and 0x11, 0 if count is 0, or "indefinitely" if count is
	 * 0xff.
	 *
	 * When used in Heartbeat publication set, this parameter denotes the
	 * number of Heartbeat messages to send.
	 *
	 * When returned from Heartbeat publication get, this parameter denotes
	 * the number of Heartbeat messages remaining to be sent.
	 */
	u8_t  count;
	/**
	 * Logarithmic Heartbeat publication transmit interval in seconds.
	 * Decoded as (1 << (period - 1)) if period is between 1 and 0x11.
	 * If period is 0, Heartbeat publication is disabled.
	 */
	u8_t  period;
	/** Publication message Time To Live value. */
	u8_t  ttl;
	/**
	 * Bitmap of features that trigger Heartbeat publications.
	 * Legal values are @ref BT_MESH_FEAT_RELAY,
	 * @ref BT_MESH_FEAT_PROXY, @ref BT_MESH_FEAT_FRIEND and
	 * @ref BT_MESH_FEAT_LOW_POWER
	 */
	u16_t feat;
	/** Network index to publish with. */
	u16_t net_idx;
};

/** @brief Set the target node's Heartbeat publication parameters.
 *
 *  @note The target node must already have received the specified network key.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param pub     New Heartbeat publication parameters.
 *  @param status  Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_hb_pub_set(u16_t net_idx, u16_t addr,
			   const struct bt_mesh_cfg_hb_pub *pub, u8_t *status);

/** @brief Get the target node's Heartbeat publication parameters.
 *
 *  @param net_idx Network index to encrypt with.
 *  @param addr    Target node address.
 *  @param pub     Heartbeat publication parameter return buffer.
 *  @param status  Status response parameter.
 *
 *  @return 0 on success, or (negative) error code on failure.
 */
int bt_mesh_cfg_hb_pub_get(u16_t net_idx, u16_t addr,
			   struct bt_mesh_cfg_hb_pub *pub, u8_t *status);

/** @brief Get the current transmission timeout value.
 *
 *  @return The configured transmission timeout.
 */
s32_t bt_mesh_cfg_cli_timeout_get(void);

/** @brief Set the transmission timeout value.
 *
 *  @param timeout The new transmission timeout.
 */
void bt_mesh_cfg_cli_timeout_set(s32_t timeout);

/** @cond INTERNAL_HIDDEN */
extern const struct bt_mesh_model_op bt_mesh_cfg_cli_op[];
extern const struct bt_mesh_model_cb bt_mesh_cfg_cli_cb;
/** @endcond */

#ifdef __cplusplus
}
#endif
/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_MESH_CFG_CLI_H_ */
