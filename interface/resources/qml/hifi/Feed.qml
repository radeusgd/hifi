//
//  Feed.qml
//  qml/hifi
//
//  Displays a particular type of feed
//
//  Created by Howard Stearns on 4/18/2017
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import Hifi 1.0
import QtQuick 2.5
import QtGraphicalEffects 1.0
import "toolbars"
import "../styles-uit"

Column {
    id: root;

    property int cardWidth: 212;
    property int cardHeight: 152;
    property int stackedCardShadowHeight: 10;
    property string metaverseServerUrl: '';
    property string actions: 'snapshot';
    onActionsChanged: fillDestinations();
    Component.onCompleted: fillDestinations();
    property string labelText: actions;
    property string filter: '';
    onFilterChanged: filterChoicesByText();

    HifiConstants { id: hifi }
    ListModel { id: suggestions; }

    function resolveUrl(url) {
        return (url.indexOf('/') === 0) ? (metaverseServerUrl + url) : url;
    }
    function makeModelData(data) { // create a new obj from data
        // ListModel elements will only ever have those properties that are defined by the first obj that is added.
        // So here we make sure that we have all the properties we need, regardless of whether it is a place data or user story.
        var name = data.place_name,
            tags = data.tags || [data.action, data.username],
            description = data.description || "",
            thumbnail_url = data.thumbnail_url || "";
        return {
            place_name: name,
            username: data.username || "",
            path: data.path || "",
            created_at: data.created_at || "",
            action: data.action || "",
            thumbnail_url: resolveUrl(thumbnail_url),
            image_url: resolveUrl(data.details.image_url),

            metaverseId: (data.id || "").toString(), // Some are strings from server while others are numbers. Model objects require uniformity.

            tags: tags,
            description: description,
            online_users: data.details.concurrency || 0,
            drillDownToPlace: false,

            searchText: [name].concat(tags, description || []).join(' ').toUpperCase()
        }
    }
    property var allStories: [];
    property var placeMap: ({}); // Used for making stacks. FIXME: generalize to not just by place.
    property int requestId: 0;
    function getUserStoryPage(pageNumber, cb) { // cb(error) after all pages of domain data have been added to model
        var options = [
            'now=' + new Date().toISOString(),
            'include_actions=' + actions,
            'restriction=' + (Account.isLoggedIn() ? 'open,hifi' : 'open'),
            'require_online=true',
            'protocol=' + encodeURIComponent(AddressManager.protocolVersion()),
            'page=' + pageNumber
        ];
        var url = metaverseBase + 'user_stories?' + options.join('&');
        var thisRequestId = ++requestId;
        getRequest(url, function (error, data) {
            if ((thisRequestId !== requestId) || handleError(url, error, data, cb)) {
                return; // abandon stale requests
            }
            allStories = allStories.concat(data.user_stories.map(makeModelData));
            if ((data.current_page < data.total_pages) && (data.current_page <=  10)) { // just 10 pages = 100 stories for now
                return getUserStoryPage(pageNumber + 1, cb);
            }
            cb();
        });
    }
    function fillDestinations() { // Public
        allStories = [];
        suggestions.clear();
        placeMap = {};
        getUserStoryPage(1, function (error) {
            allStories.forEach(makeFilteredStoryProcessor());
            console.log('user stories query', actions, error || 'ok', allStories.length, 'filtered to', suggestions.count);
        });
    }
    function addToSuggestions(place) { // fixme: move to makeFilteredStoryProcessor
        var collapse = (actions === 'snapshot,concurrency') && (place.action !== 'concurrency'); // fixme generalize?
        if (collapse) {
            var existing = placeMap[place.place_name];
            if (existing) {
                existing.drillDownToPlace = true;
                return;
            }
        }
        suggestions.append(place);
        if (collapse) {
            placeMap[place.place_name] = suggestions.get(suggestions.count - 1);
        } else if (place.action === 'concurrency') {
            suggestions.get(suggestions.count - 1).drillDownToPlace = true; // Don't change raw place object (in allStories).
        }
    }
    function suggestable(story) { // fixme add to makeFilteredStoryProcessor
        if (story.action === 'snapshot') {
            return true;
        }
        return (story.place_name !== AddressManager.placename); // Not our entry, but do show other entry points to current domain.
    }
    function makeFilteredStoryProcessor() { // answer a function(storyData) that adds it to suggestions if it matches
        var words = filter.toUpperCase().split(/\s+/).filter(identity);
        function matches(story) {
            if (!words.length) {
                return suggestable(story);
            }
            return words.every(function (word) {
                return story.searchText.indexOf(word) >= 0;
            });
        }
        return function (story) {
            if (matches(story)) {
                addToSuggestions(story);
            }
        };
    }
    function filterChoicesByText() {
        suggestions.clear();
        placeMap = {};
        allStories.forEach(makeFilteredStoryProcessor());
    }

    RalewayLight {
        id: label;
        text: labelText;
        color: hifi.colors.blueHighlight;
        size: 28;
    }
    ListView {
        id: scroll;
        clip: true;
        model: suggestions;
        orientation: ListView.Horizontal;

        spacing: 14;
        width: parent.width;
        height: cardHeight + stackedCardShadowHeight;
        delegate: Card {
            width: cardWidth;
            height: cardHeight;
            goFunction: goCard; // fixme global
            userName: model.username;
            placeName: model.place_name;
            hifiUrl: model.place_name + model.path;
            thumbnail: model.thumbnail_url;
            imageUrl: model.image_url;
            action: model.action;
            timestamp: model.created_at;
            onlineUsers: model.online_users;
            storyId: model.metaverseId;
            drillDownToPlace: model.drillDownToPlace;
            shadowHeight: stackedCardShadowHeight;
            hoverThunk: function () { scroll.currentIndex = index; }
            unhoverThunk: function () { scroll.currentIndex = -1; }
        }
    }
}
